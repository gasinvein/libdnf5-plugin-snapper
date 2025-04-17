Name:           libdnf5-plugin-snapper
Version:        0.1.0
Release:        0%{?dist}
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
