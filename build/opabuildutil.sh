
CC="${CC:-gcc}"
AR="${AR:-ar}"
ARFLAGS="${ARFLAGS:--rcs}"

if [ "$OS" = "Windows_NT" ]; then
	# running mingw on windows
	TGTOS="${TGTOS:-win}"
else
	TGTOS="${TGTOS:-$(uname | tr '[:upper:]' '[:lower:]')}"
fi

UNAME="${UNAME:-$(uname | tr '[:upper:]' '[:lower:]')}"

getnproc() {
	if [ "$UNAME" = "linux" ] || [ "$OS" = "Windows_NT" ]; then
		nproc 2>/dev/null || echo 1
	elif [ "$UNAME" = "darwin" ]; then
		sysctl -n hw.logicalcpu 2>/dev/null || echo 1
	elif [ "$UNAME" = "freebsd" ]; then
		sysctl -n hw.ncpu 2>/dev/null || echo 1
	else
		echo 1
	fi
}

NPROC="${NPROC:-$(getnproc)}"

if [ "$NPROC" -gt 1 ]; then
	BUILDDIR_MP="${BUILDDIR_MP:-$((NPROC*4))}"
else
	BUILDDIR_MP="${BUILDDIR_MP:-1}"
fi

testgccopt() {
	($CC -E -Werror "$1" - < /dev/null > /dev/null 2>&1 && echo "$1") || echo ""
}

GCCWARN="-Wall -Wextra"
GCCWARN="$GCCWARN $(testgccopt -Wpedantic)"
GCCWARN="$GCCWARN $(testgccopt -Wshadow)"
GCCWARN="$GCCWARN $(testgccopt -Wmissing-prototypes)"
GCCWARN="$GCCWARN $(testgccopt -Wstrict-prototypes)"
GCCWARN="$GCCWARN $(testgccopt -Wbad-function-cast)"
GCCWARN="$GCCWARN $(testgccopt -Wcast-align)"
GCCWARN="$GCCWARN $(testgccopt -Wno-missing-field-initializers)"

# additional warnings from https://kristerw.blogspot.com/2017/09/useful-gcc-warning-options-not-enabled.html
GCCWARN="$GCCWARN $(testgccopt -Wduplicated-cond)"
GCCWARN="$GCCWARN $(testgccopt -Wduplicated-branches)"
#GCCWARN="$GCCWARN $(testgccopt -Wlogical-op)"
GCCWARN="$GCCWARN $(testgccopt -Wrestrict)"
GCCWARN="$GCCWARN $(testgccopt -Wnull-dereference)"
GCCWARN="$GCCWARN $(testgccopt -Wjump-misses-init)"
GCCWARN="$GCCWARN $(testgccopt -Wdouble-promotion)"
GCCWARN="$GCCWARN $(testgccopt -Wformat=2)"

STRIPALLFLAG="-s"
if [ "$UNAME" = "darwin" ]; then
	STRIPALLFLAG=""
fi

# $1 is .c file
# $2 is destination directory for .o file
buildcfile() {
	FNAME=$(basename "$1" .c)
	# cd to directory containing .c file so that __FILE__ does not contain directories
	ORIGDIR=$(pwd)
	cd "$(dirname "$1")" || exit 1
	if [ "$3" != "MP" ]; then
		echo "building $FNAME.c"
	fi
	$CC -c $GCCWARN $DEFS $INCS $CFLAGS -o "$2/$FNAME.o" "$FNAME.c" || exit 1
	if [ "$3" = "MP" ]; then
		echo "built $FNAME.c"
	fi
	cd "$ORIGDIR" || exit 1
}

#function buildcfile {
#	if [ "$(jobs -p | wc -l)" -ge $NPROC ]; then
#		wait -n
#	fi
#	buildcfile2 $1 $2 "MP" &
#}

# $1 is directory of .c files to build
# $2 is destination dir
builddir() {
	if [ "$BUILDDIR_MP" -gt 1 ]; then
		idx=0
		for fname in "$1"/*.c; do
			idx=$((idx+1))
			buildcfile "$fname" "$2" "MP" &
			if [ $((idx % BUILDDIR_MP)) -eq 0 ]; then
				wait
			fi
		done
		wait
		# determine whether any file failed to build
		for i in "$1"/*.c; do
			if [ ! -f "$2/$(basename "$i" .c).o" ]; then
				echo "error occurred; exiting"
				exit 1
			fi
		done
	else
		for i in "$1"/*.c; do
			buildcfile "$i" "$2"
		done
	fi
}

deldir() {
	if [ -d "$1" ]; then
		rm -rf "$1"
	fi
}

cleandir() {
	deldir "$1"
	mkdir "$1"
}

ensuredir() {
	if [ ! -d "$1" ]; then
		mkdir "$1"
	fi
}
