%define name powerpc-utils
%define version 1.2.2
%define release 0
Summary:	Utilities for PowerPC platforms
Name:		%{name}
Version:	%{version}
Release:	%{release}
License:	IBM Common Public License (CPL)
Group:		System Environment
Source0:	N/A
BuildRoot:	/tmp/%{name}-buildroot/
Requires:	/bin/bash, /bin/sh, /bin/sed, /usr/bin/perl, librtas >= 1.3.0

%description
Utilities for maintaining and servicing PowerPC systems.

%files 
%defattr(-,root,root)
/usr/share/doc/packages/powerpc-utils/README
/usr/share/doc/packages/powerpc-utils/COPYRIGHT

/usr/sbin/update_flash
/usr/sbin/activate_firmware
/usr/sbin/usysident
/usr/sbin/usysattn
/usr/sbin/set_poweron_time
/usr/sbin/rtas_ibm_get_vpd
/usr/sbin/serv_config
/usr/sbin/uesensor
/usr/sbin/hvcsadmin
/usr/sbin/vscsisadmin
/usr/sbin/rtas_dump
/usr/sbin/rtas_event_decode
/usr/sbin/sys_ident
/usr/sbin/drmgr
/usr/sbin/lsslot
/usr/sbin/lsprop
/usr/sbin/nvram
/usr/sbin/snap
/usr/sbin/bootlist
/usr/sbin/ofpathname
/usr/sbin/ppc64_cpu
/usr/sbin/lsdevinfo

/etc/init.d/ibmvscsis.sh

/usr/bin/amsstat

/usr/share/man/man8/update_flash.8
/usr/share/man/man8/activate_firmware.8
/usr/share/man/man8/usysident.8
/usr/share/man/man8/usysattn.8
/usr/share/man/man8/set_poweron_time.8
/usr/share/man/man8/rtas_ibm_get_vpd.8
/usr/share/man/man8/serv_config.8
/usr/share/man/man8/uesensor.8
/usr/share/man/man8/hvcsadmin.8
/usr/share/man/man8/vscsisadmin.8
/usr/share/man/man8/ibmvscsis.sh.8
/usr/share/man/man8/ibmvscsis.conf.8
/usr/share/man/man8/rtas_dump.8
/usr/share/man/man8/sys_ident.8
/usr/share/man/man8/nvram.8
/usr/share/man/man8/snap.8
/usr/share/man/man8/bootlist.8
/usr/share/man/man8/ofpathname.8

/usr/share/man/man1/amsstat.1

%post
# Post-install script -----------------------------------------------
ln -sf /usr/sbin/usysattn usr/sbin/usysfault
ln -sf /usr/sbin/serv_config usr/sbin/uspchrp
ln -sf /usr/share/man/man8/usysattn.8 usr/share/man/man8/usysfault.8
ln -sf /usr/share/man/man8/serv_config.8 usr/share/man/man8/uspchrp.8
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
	rm -f /usr/sbin/usysfault
	rm -f /usr/sbin/uspchrp
	rm -f /usr/share/man/man8/usysfault.8.gz
	rm -f /usr/share/man/man8/uspchrp.8.gz
	rm /usr/sbin/drslot_chrp_slot
        rm /usr/sbin/drslot_chrp_pci
        rm /usr/sbin/drslot_chrp_cpu
        rm /usr/sbin/drslot_chrp_phb
        rm /usr/sbin/drslot_chrp_mem
        rm /usr/sbin/drslot_chrp_hea
        rm /usr/sbin/drmig_chrp_pmig
fi

