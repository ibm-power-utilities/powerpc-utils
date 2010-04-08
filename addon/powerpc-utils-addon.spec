%define name powerpc-utils-sles10-addons
%define version 1.0.0
%define release 0
Summary:	Powerpc-utils Add-ons for SLES10 SP3
Name:		%{name}
Version:	%{version}
Release:	%{release}
License:	IBM Common Public License (CPL)
Group:		System Environment
Source:		powerpc-utils-sles10-addons-%{version}.tar.gz
BuildRoot:	/tmp/%{name}-buildroot/
Requires:	/bin/bash, /bin/sh, /bin/sed, /usr/bin/perl, librtas >= 1.3.0

%description
Additional utilities for maintaining and servicing PowerPC systems on SLES10
SP3 releases.

%prep
%setup -q

%build
%configure
%{__make} %{?_smp_mflags}

%install
%{__rm} -rf $RPM_BULD_ROOT
%{__make} install -C addon DESTDIR=$RPM_BUILD_ROOT

%files
%defattr(-,root,root)
#/usr/share/doc/packages/powerpc-utils-sles10-addons/README
#/usr/share/doc/packages/powerpc-utils-sles10-addons/COPYRIGHT

/usr/sbin/drmgr
/usr/sbin/lsslot
/usr/sbin/lsdevinfo
/usr/sbin/ls-veth
/usr/sbin/ls-vdev
/usr/sbin/ls-vscsi

%post
# Post-install script -----------------------------------------------
ln -sf /usr/sbin/drmgr /usr/sbin/drslot_chrp_slot
ln -sf /usr/sbin/drmgr /usr/sbin/drslot_chrp_pci
ln -sf /usr/sbin/drmgr /usr/sbin/drslot_chrp_cpu
ln -sf /usr/sbin/drmgr /usr/sbin/drslot_chrp_phb
ln -sf /usr/sbin/drmgr /usr/sbin/drslot_chrp_mem
ln -sf /usr/sbin/drmgr /usr/sbin/drslot_chrp_hea
ln -sf /usr/sbin/drmgr /usr/sbin/drmig_chrp_pmig

%postun
# Post-uninstall script ---------------------------------------------
if [ "$1" = "0" ]; then
	# last uninstall
	rm /usr/sbin/drslot_chrp_slot
        rm /usr/sbin/drslot_chrp_pci
        rm /usr/sbin/drslot_chrp_cpu
        rm /usr/sbin/drslot_chrp_phb
        rm /usr/sbin/drslot_chrp_mem
        rm /usr/sbin/drslot_chrp_hea
        rm /usr/sbin/drmig_chrp_pmig
fi

