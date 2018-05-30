#%global ponyc_version %(cat ../../VERSION)
%global ponyc_version 0.22.2
%global release_version 1

%ifarch x86_64
%global arch_build_args arch=x86-64 tune=intel
%endif

%if 0%{?el7}
%global extra_build_args use="llvm_link_static"
%global build_command_prefix scl enable llvm-toolset-7 
%else
%global extra_build_args default_ssl='openssl_1.1.0'
%endif

%global all_env_vars5 %(ls -l %{_sourcedir})

%{echo:%{all_env_vars5}}

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
BuildRequires:  pcre2-devel
BuildRequires:  zlib-devel
BuildRequires:  ncurses-devel

%if %{?_vendor} == suse
BuildRequires:  libopenssl-devel
BuildRequires:  binutils-gold
%else
BuildRequires:  openssl-devel
BuildRequires:  libatomic
%endif

%if 0%{?el7}
BuildPreReq:  centos-release-scl
BuildRequires:  llvm-toolset-7
BuildRequires:  llvm-toolset-7-llvm-devel
BuildRequires:  llvm-toolset-7-llvm-static
BuildRequires:  libatomic
%else
BuildRequires:  llvm-devel
%endif

Requires:  gcc-c++
Requires:  openssl-devel
Requires:  pcre2-devel

%description
Compiler for the pony programming language.

%prep
%setup

%build
%{?build_command_prefix} make %{?arch_build_args} %{?extra_build_args} prefix=/usr %{?_smp_mflags} test-ci

%install
%{?build_command_prefix} make install %{?arch_build_args} %{?extra_build_args} prefix=%{_prefix} DESTDIR=$RPM_BUILD_ROOT

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
