/*
 * Re-map bind() on 0.0.0.0 or :: to bind() on the node's public IP address
 * Jude Nelson (jcnelson@cs.princeton.edu)
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <limits.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <dlfcn.h>

int (*bind_original)(int fd, struct sockaddr* addr, socklen_t len ) = NULL;

// which C library do we need to replace bind in?
#if defined(__LP64__) || defined(_LP64)
#define LIBC_PATH "/lib64/libc.so.6"
#else
#define LIBC_PATH "/lib/libc.so.6"
#endif

// get the node's public IP address
static int get_public_ip( struct sockaddr* addr ) {
   
   struct addrinfo hints;
   memset( &hints, 0, sizeof(hints) );
   hints.ai_family = addr->sa_family;
   hints.ai_flags = AI_CANONNAME;
   hints.ai_protocol = 0;
   hints.ai_canonname = NULL;
   hints.ai_addr = NULL;
   hints.ai_next = NULL;
   
   int rc = 0;

   // get the node hostname
   struct addrinfo *result = NULL;
   char hostname[HOST_NAME_MAX+1];
   gethostname( hostname, HOST_NAME_MAX );

   // get the address information from the hostname
   rc = getaddrinfo( hostname, NULL, &hints, &result );
   if( rc != 0 ) {
      // could not get addr info
      fprintf(stderr, "bind_public: get_public_ip: getaddrinfo: %s\n", gai_strerror( rc ) );
      errno = EINVAL;
      return -1;
   }
   
   // NOTE: there should only be one IP address for this node, but it
   // is possible that it can have more.  Here, we just take the first
   // address given.
   
   switch( addr->sa_family ) {
      case AF_INET:
         // IPv4
         ((struct sockaddr_in*)addr)->sin_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr;
         break;
         
      case AF_INET6:
         // IPv6
         ((struct sockaddr_in6*)addr)->sin6_addr = ((struct sockaddr_in6*)result->ai_addr)->sin6_addr;
         break;
         
      default:
         fprintf(stderr, "bind_public: get_public_ip: unknown socket address family %d\n", addr->sa_family );
         rc = -1;
         break;
   }
   
   freeaddrinfo( result );
   
   return rc;
}


// is a particular sockaddr initialized to 0.0.0.0 or ::?
static int is_addr_any( const struct sockaddr* addr ) {
   int ret = 0;
   
   switch( addr->sa_family ) {
      case AF_INET: {
         // IPv4
         struct sockaddr_in* addr4 = (struct sockaddr_in*)addr;
         if( addr4->sin_addr.s_addr == INADDR_ANY )
            ret = 1;    // this is 0.0.0.0
         break;
      }
      case AF_INET6: {
         // IPv6
         struct sockaddr_in6* addr6 = (struct sockaddr_in6*)addr;
         if( memcmp( &addr6->sin6_addr, &in6addr_any, sizeof(in6addr_any) ) == 0 )
            ret = 1;    // this is ::
         break;
      }
      default:
         // unsupported bind
         fprintf(stderr, "bind_public: is_addr_any: unsupported socket address family %d\n", addr->sa_family );
         ret = -1;
         break;
   }
   
   return ret;
}


// copy over non-IP-related fields from one address to another
static int copy_nonIP_fields( struct sockaddr* dest, const struct sockaddr* src ) {
   int rc = 0;
   
   switch( src->sa_family ) {
      case AF_INET: {
         // IPv4
         struct sockaddr_in* dest4 = (struct sockaddr_in*)dest;
         struct sockaddr_in* src4 = (struct sockaddr_in*)src;
         
         dest4->sin_family = src4->sin_family;
         dest4->sin_port = src4->sin_port;
         break;
      }
      case AF_INET6: {
         // IPv6
         struct sockaddr_in6* dest6 = (struct sockaddr_in6*)dest;
         struct sockaddr_in6* src6 = (struct sockaddr_in6*)src;
         
         dest6->sin6_family = src6->sin6_family;
         dest6->sin6_port = src6->sin6_port;
         dest6->sin6_flowinfo = src6->sin6_flowinfo;
         dest6->sin6_scope_id = src6->sin6_scope_id;
         break;
      }
      default:
         // unsupported bind
         fprintf(stderr, "bind_public: copy_nonIP_fields: unsupported socket address family %d\n", src->sa_family );
         rc = -1;
         break;
   }
   
   return rc;
}


static void print_ip4( uint32_t i ) {
   i = htonl( i );
   printf("%i.%i.%i.%i",
          (i >> 24) & 0xFF,
          (i >> 16) & 0xFF,
          (i >> 8) & 0xFF,
          i & 0xFF);
}

static void print_ip6( uint8_t* bytes ) {
   printf("%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x",
          bytes[15], bytes[14], bytes[13], bytes[12],
          bytes[11], bytes[10], bytes[9],  bytes[8],
          bytes[7],  bytes[6],  bytes[5],  bytes[4],
          bytes[3],  bytes[2],  bytes[1],  bytes[0] );
}

static void debug( const struct sockaddr* before, struct sockaddr* after ) {
   printf("bind_public: ");
   switch( before->sa_family ) {
      case AF_INET:
         print_ip4( ((struct sockaddr_in*)before)->sin_addr.s_addr );
         printf(" --> ");
         print_ip4( ((struct sockaddr_in*)after)->sin_addr.s_addr );
         printf("\n");
         break;
      case AF_INET6:
         print_ip6( ((struct sockaddr_in6*)before)->sin6_addr.s6_addr );
         printf(" --> " );
         print_ip6( ((struct sockaddr_in6*)after)->sin6_addr.s6_addr );
         printf("\n");
         break;
      default:
         printf("UNKNOWN --> UNKNOWN\n");
         break;
   }
   fflush( stdout );
}

// if the caller attempted to bind to 0.0.0.0 or ::, then change it to
// this node's public IP address
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {

   // save the original bind() call
   void *handle = dlopen( LIBC_PATH, RTLD_LAZY );
   if (!handle) {
      fprintf( stderr, "Error loading libc.so.6\n" );
      fflush( stderr );
      return -1;
   }
   bind_original = dlsym(handle, "bind");
   if( bind_original == NULL ) {
      fprintf( stderr, "Error loading socket symbol\n" );
      fflush( stderr );
      return -1;
   }

   int rc = is_addr_any( addr );
   if( rc > 0 ) {

      // rewrite this address
      struct sockaddr_storage new_addr;
      memset( &new_addr, 0, sizeof(struct sockaddr_storage));

      if( copy_nonIP_fields( (struct sockaddr*)&new_addr, addr ) != 0 ) {
         errno = EACCES;
         rc = -1;
      }
      else if( get_public_ip( (struct sockaddr*)&new_addr ) != 0 ) {
         rc = -1;
      }
      else {
         // Un-comment the following line to activate the debug message
         //debug( addr, (struct sockaddr*)&new_addr );
         rc = bind_original( sockfd, (struct sockaddr*)&new_addr, addrlen );
      }
   }
   else {
      return bind_original( sockfd, addr, addrlen );
   }
}
