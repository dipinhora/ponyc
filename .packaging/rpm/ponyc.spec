%global ponyc_version %(cat VERSION)
%global release_version 1

%ifarch x86_64
%global extra_build_args arch=x86-64 tune=intel
%endif

%dump

Name:       ponyc
Version:    %{ponyc_version}
Release:    %{release_version}%{?dist}
Summary:     Compiler for the pony programming language.
# For a breakdown of the licensing, see PACKAGE-LICENSING
License:    BSD
URL:        https://github.com/ponylang/ponyc
Source0:    https://github.com/ponylang/ponyc/archive/%{version}.tar.gz
BuildRequires:  git
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  openssl-devel
BuildRequires:  pcre2-devel
BuildRequires:  zlib-devel
BuildRequires:  llvm-devel
BuildRequires:  ncurses-devel

%if 0%{?el#}
BuildRequires:  libatomic
%endif

Requires:  gcc-c++
Requires:  openssl-devel
Requires:  pcre2-devel

%description
Compiler for the pony programming language.

%build
make %{extra_build_args} prefix=/usr %{?_smp_mflags}

%install
make install %{extra_build_args} prefix=%_prefix DESTDIR=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%{_prefix}/bin/ponyc
%{_prefix}/lib/libponyrt-pic.a
%{_prefix}/lib/libponyc.a
%{_prefix}/lib/pony
%{_prefix}/lib/libponyrt.a
%{_prefix}/include/pony.h
%{_prefix}/include/pony

%changelog
* Tue May 29 2018 Dipin Hora <dipin@wallaroolabs.com> 0.22.2-1
- Initial version
