%define _prefix /opt/samplicator

Name: samplicator
Version: 1.3.6
Release: 1.ceeb1d2%{?dist}
Summary: Samplicator
AutoReqProv: no

License: GPLv2
URL: https://github.com/sleinen/samplicator.git

Source0: https://github.com/sleinen/samplicator/archive/ceeb1d280188c155b71d819282490be86190f6f6.zip

BuildRequires: automake make gcc
#Requires:

%description
Send copies of (UDP) datagrams to multiple receivers, with optional sampling and spoofing

%prep
%setup -q -n samplicator-ceeb1d280188c155b71d819282490be86190f6f6

%build
./autogen.sh
%configure
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}
mkdir -p %{buildroot}/usr/lib/systemd/system/
cp samplicator.service %{buildroot}/usr/lib/systemd/system/

%clean
# noop


%files
%defattr(-,root,root,-)

/opt/samplicator/bin/samplicate
/usr/lib/systemd/system/samplicator.service

%changelog