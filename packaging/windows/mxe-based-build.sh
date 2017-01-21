#!/bin/bash -e

# build Subsurface for Win32
#
# this file assumes that you have installed MXE on your system
# and installed a number of dependencies as well
#
# cd ~/src/win
# git clone https://github.com/mxe/mxe
# cd mxe
#
# now create a file settings.mk
#---
# # This variable controls the number of compilation processes
# # within one package ("intra-package parallelism").
# JOBS := 12
#
# # This variable controls the targets that will build.
# MXE_TARGETS :=  i686-w64-mingw32.shared
#---
# (documenting this in comments is hard... you need to remove
# the first '#' of course)
#
# now you can start the build
#
# make libxml2 libxslt libusb1 libzip qt5 nsis
#
# After quite a while (depending on your machine anywhere from 15-20
# minutes to several hours) you should have a working MXE install in
# ~/src/win/mxe
#
# Now this script will come in:
#
# This makes some assumption about the filesystem layout based
# on the way things are setup on my system so I can build Ubuntu PPA,
# OBS and Windows out of the same sources.
# Something like this:
#
# ~/src/win/mxe                    <- current MXE git with Qt5, automake (see above)
#      /win/grantlee               <- Grantlee 5.0.0 sources from git
#      /win/libssh2                <- from git - v1.6 seems to work
#      /win/libcurl                <- from git - 7.42.1 seems to work
#      /win/subsurface             <- current subsurface git
#      /win/libdivecomputer        <- appropriate libdc/Subsurface-branch branch
#      /win/marble-source          <- appropriate marble/Subsurface-branch branch
#      /win/libgit2                <- libgit2 0.23.1 or similar
#
# ~/src/win/win32                  <- build directory
#
# then start this script from ~/src/win/win32
#
#  cd ~/src/win/win32
#  bash ../../subsurface/packaging/windows/mxe-based-build.sh installer
#
# this should create the latest daily installer
#
# in order not to keep recompiling everything this script only compiles
# the other components if their directories are missing or if a "magic
# file" has been touched.
#
# so if you update one of the other libs do
#
# cd ~/src/win/win32
# touch build.<component>
# bash ../../subsurface/packaging/windows/mxe-based-build.sh installer
#
# and that component gets rebuilt as well. E.g.
# touch build.libdivecomputer
# to rebuild libdivecomputer before you build Subsurface
#
# please send patches / additions to this file!
#

exec 1> >(tee ./winbuild.log) 2>&1

# for debugging
#trap "set +x; sleep 1; set -x" DEBUG

# this is run on a rather powerful machine - if you want less
# build parallelism, please change this variable
JOBS="-j4"

EXECDIR=`pwd`
BASEDIR=$(cd "$EXECDIR/.."; pwd)
BUILDDIR=$(cd "$EXECDIR"; pwd)

echo $BUILDDIR

if [[ ! -d "$BASEDIR"/mxe ]] ; then
	echo "Please start this from the right directory "
	echo "usually a winbuild directory parallel to the mxe directory"
	exit 1
fi

echo "Building in $BUILDDIR ..."

export PATH="$BASEDIR"/mxe/usr/bin:$PATH:"$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5/bin/

if [[ "$1" == "debug" ]] ; then
	RELEASE="Debug"
	shift
else
	RELEASE="Release"
fi

# grantlee

cd "$BUILDDIR"
if [[ ! -d grantlee || -f build.grantlee ]] ; then
	rm -f build.grantlee
	mkdir -p grantlee
	cd grantlee
	cmake -DCMAKE_TOOLCHAIN_FILE="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/share/cmake/mxe-conf.cmake \
		-DCMAKE_BUILD_TYPE=$RELEASE \
		-DBUILD_TESTS=OFF \
		"$BASEDIR"/grantlee

	make $JOBS
	make install
fi


# libssh2:

cd "$BUILDDIR"
if [[ ! -d libssh2 || -f build.libssh2 ]] ; then
	rm -f build.libssh2
	mkdir -p libssh2
	cd libssh2

	cmake -DCMAKE_TOOLCHAIN_FILE="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/share/cmake/mxe-conf.cmake \
		-DCMAKE_BUILD_TYPE=$RELEASE \
		-DBUILD_EXAMPLES=OFF \
		-DBUILD_TESTING=OFF \
		-DBUILD_SHARED_LIBS=ON \
		"$BASEDIR"/libssh2
	make $JOBS
	make install
	# don't install your dlls in bin, please
	cp "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/bin/libssh2.dll "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/lib
fi


# libcurl

cd "$BUILDDIR"
if [[ ! -d libcurl || -f build.libcurl ]] ; then
	rm -f build.libcurl
	mkdir -p libcurl
	cd libcurl
	../../libcurl/configure --host=i686-w64-mingw32.shared \
		--prefix="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/ \
		--disable-ftp \
		--disable-ldap \
		--disable-ldaps \
		--disable-rtsp \
		--enable-proxy \
		--enable-dict \
		--disable-telnet \
		--disable-tftp \
		--disable-pop3 \
		--disable-imap \
		--disable-smb \
		--disable-smtp \
		--disable-gopher \
		--disable-manual \
		--with-libssh2="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/

	# now remove building the executable
	sed -i 's/SUBDIRS = lib src include/SUBDIRS = lib include/' Makefile

	make $JOBS
	make install
fi


# libgit2:

cd "$BUILDDIR"
if [[ ! -d libgit2 || -f build.libgit2 ]] ; then
	rm -f build.libgit2
	mkdir -p libgit2
	cd libgit2
	cmake -DCMAKE_TOOLCHAIN_FILE="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/share/cmake/mxe-conf.cmake \
		-DBUILD_CLAR=OFF -DTHREADSAFE=ON \
		-DCMAKE_BUILD_TYPE=$RELEASE \
		-DDLLTOOL="$BASEDIR"/mxe/usr/bin/i686-w64-mingw32.shared-dlltool \
		"$BASEDIR"/libgit2
	make $JOBS
	make install
fi

# libdivecomputer
#
# this one is special because we want to make sure it's in sync
# with the Linux builds, but we don't want the autoconf files cluttering
# the original source directory... so the "$BASEDIR"/libdivecomputer is
# a local clone of the "real" libdivecomputer directory

cd "$BUILDDIR"
if [[ ! -d libdivecomputer || -f build.libdivecomputer ]] ; then
	rm build.libdivecomputer
	cd "$BASEDIR"/libdivecomputer
	git pull
	cd "$BUILDDIR"
	mkdir -p libdivecomputer
	cd libdivecomputer

	"$BASEDIR"/libdivecomputer/configure --host=i686-w64-mingw32.shared \
		--enable-shared \
		--prefix="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared
	make $JOBS
	make install
else
	echo ""
	echo ""
	echo "WARNING!!!!"
	echo ""
	echo "libdivecoputer not rebuilt!!"
	echo ""
	echo ""
fi

# marble:

cd "$BUILDDIR"
if [[ ! -d marble || -f build.marble ]] ; then
	rm build.marble
	mkdir -p marble
	cd marble
	cmake -DCMAKE_TOOLCHAIN_FILE="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/share/cmake/mxe-conf.cmake \
		-DCMAKE_PREFIX_PATH="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5 \
		-DQTONLY=ON -DQT5BUILD=ON \
		-DBUILD_MARBLE_APPS=OFF -DBUILD_MARBLE_EXAMPLES=OFF \
		-DBUILD_MARBLE_TESTS=OFF -DBUILD_MARBLE_TOOLS=OFF \
		-DBUILD_TESTING=OFF -DWITH_DESIGNER_PLUGIN=OFF \
		-DBUILD_WITH_DBUS=OFF \
		-DCMAKE_BUILD_TYPE=$RELEASE \
		"$BASEDIR"/marble-source
	make $JOBS
	make install
	# what the heck is marble doing?
	mv "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/libssrfmarblewidget.dll "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/lib
fi

###############
# finally, Subsurface

cd "$BUILDDIR"
echo "Starting Subsurface Build"

# things go weird if we don't create a new build directory... Subsurface
# suddenly gets linked against Qt5Guid.a etc...
rm -rf subsurface

# first copy the Qt plugins in place
mkdir -p subsurface/staging/plugins
cd subsurface/staging/plugins
cp -a "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5/plugins/iconengines .
cp -a "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5/plugins/imageformats .
cp -a "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5/plugins/platforms .
cp -a "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5/plugins/printsupport .

# for some reason we aren't installing libssrfmarblewidget.dll and # Qt5Xml.dll
# I need to figure out why and fix that, but for now just manually copy that as well
cp "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/lib/libssrfmarblewidget.dll "$BUILDDIR"/subsurface/staging
cp "$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5/bin/Qt5Xml.dll "$BUILDDIR"/subsurface/staging

cd "$BUILDDIR"/subsurface

cmake -DCMAKE_TOOLCHAIN_FILE="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/share/cmake/mxe-conf.cmake \
	-DCMAKE_PREFIX_PATH="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5 \
	-DCMAKE_BUILD_TYPE=$RELEASE \
	-DQT_TRANSLATION_DIR="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/qt5/translations \
	-DMAKENSIS=i686-w64-mingw32.shared-makensis \
	-DLIBDIVECOMPUTER_INCLUDE_DIR="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/include \
	-DLIBDIVECOMPUTER_LIBRARIES="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/lib/libdivecomputer.dll.a \
	-DMARBLE_INCLUDE_DIR="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/include \
	-DMARBLE_LIBRARIES="$BASEDIR"/mxe/usr/i686-w64-mingw32.shared/lib/libssrfmarblewidget.dll \
	-DMAKE_TESTS=OFF \
	"$BASEDIR"/subsurface

make $JOBS "$@"
