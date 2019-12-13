#/bin/sh
mkdir -p pkg/pi
rm -rf pkg/pi.old
mv pkg/pi pkg/pi.old
mkdir -p pkg/pi
cd pkg/pi

if [ "$NOPACK" ] ; 
then
    echo dev mode - do not package
    exit 0
fi

SRCDIR=../../..

#now create debian packages
mkdir rtpmidid_deb 
cd rtpmidid_deb 
mkdir DEBIAN
cp -R ${SRCDIR}/packaging/rtpmidid_pkg/* DEBIAN
mkdir -p etc/systemd/system
cp ${SRCDIR}/resources/rtpmidid.service etc/systemd/system
mkdir -p usr/local/bin
cp -R ${SRCDIR}/build/src/rtpmidid usr/local/bin

cd ..
fakeroot dpkg --build rtpmidid_deb
mv rtpmidid_deb.deb rtpmidid.deb
rm -rf rtpmidid_deb


