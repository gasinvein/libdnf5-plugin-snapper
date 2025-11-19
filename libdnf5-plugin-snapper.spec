%global commit      %(%{__git} rev-parse HEAD)
%global shortcommit %(c=%{commit}; echo ${c:0:7})
%global date        %(date -d "@$(%{__git} show -s --format='format:%%ct')" +'%%Y%%m%%d')

Name:           libdnf5-plugin-snapper
Version:        0.2.0
Release:        0.%{date}git%{shortcommit}%{?dist}
Summary:        Snapper plugin for libdnf5

License:        LGPL-2.1
URL:            https://github.com/gasinvein/%{name}

BuildRequires:  meson
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(libdnf5)
BuildRequires:  snapper-devel


%description
A libdnf5 plugin that automatically creates Snapper snapshots before and after
DNF transaction.


%build
%meson
%meson_build


%install
%meson_install


%files
%config(noreplace) %{_sysconfdir}/dnf/libdnf5-plugins/snapper.conf
%{_libdir}/libdnf5/plugins/snapper.so


%changelog
%autochangelog
