#!/bin/sh -e

NAME=`head -n 1 debian/changelog | perl -pe 's/^.*\((.*)(\d+)\).*$/$1/'`
VERSION=`head -n 1 debian/changelog | perl -pe 's/^.*\(.*(\d+)\).*$/$1/'`

echo "Build $NAME$VERSION"

rm -f debug

debuild clean
debuild -us -uc -sa

NEW_VERSION="$NAME$VERSION.debug"

dch -v $NEW_VERSION "Debug version."
echo "Build $NEW_VERSION"

touch debug

debuild clean
debuild -us -uc -sa

rm debug
debuild clean

