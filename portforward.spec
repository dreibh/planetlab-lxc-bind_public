%define name portforward
%define version 0.1
%define taglevel 1

%define release %{taglevel}%{?pldistro:.%{pldistro}}%{?date:.%{date}}

Summary: Library for transparently redirect bind() calls for lxc-PlanetLab
Name: %{name}
Version: %{version}
Release: %{release}
License: PlanetLab
Group: System Environment/Libraries
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

Vendor: PlanetLab
Packager: PlanetLab Central <support@planet-lab.org>
Distribution: PlanetLab %{plrelease}
URL: %{SCMURL}

%description
This package provides bind_public.so that allows to
transparently redirect calls to bind(0.0.0.0) to the public IP address
of the node.

%prep
%setup -q

%build
make

%install
make install DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
/etc/planetlab/

%changelog
