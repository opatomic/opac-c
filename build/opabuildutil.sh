
CC="${CC:-gcc}"
AR="${AR:-ar}"
ARFLAGS="${ARFLAGS:--rcs}"
ccache --version > /dev/null 2>&1 && CCACHE="${CCACHE-ccache}" || CCACHE="${CCACHE-}"

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
	if $CC -E -Werror $@ - < /dev/null > /dev/null 2>&1 ; then
		echo "$@"
	else
		RES=""
		for i in "$@"; do
			$CC -E -Werror "$i" - < /dev/null > /dev/null 2>&1 && RES="$RES $i"
		done
		echo "$RES"
	fi
}

# additional warnings from https://kristerw.blogspot.com/2017/09/useful-gcc-warning-options-not-enabled.html
GCCWARN="$(testgccopt -Wall -Wextra -Wpedantic -Wshadow -Wmissing-prototypes -Wstrict-prototypes -Wbad-function-cast -Wcast-align -Wno-missing-field-initializers -Wduplicated-cond -Wduplicated-branches -Wrestrict -Wnull-dereference -Wjump-misses-init -Wdouble-promotion -Wformat=2 -Wdate-time)"

STRIPALLFLAG="-s"
if [ "$UNAME" = "darwin" ]; then
	STRIPALLFLAG=""
fi

# $1 is .c file
# $2 is destination directory for .o file
buildcfile() {
	FNAME=$(basename "$1" .c)
	DSTDIR=$( cd "$2" && pwd ) || exit 1
	if [ "$PBFN" = "" ]; then
		# cd to directory containing .c file so that __FILE__ does not contain directories
		ORIGDIR="$PWD"
		cd "$(dirname "$1")" || exit 1
		if [ "$3" != "MP" ]; then
			echo "building $FNAME.c"
		fi
		$CCACHE $CC -c $GCCWARN $DEFS $INCS $CFLAGS -o "$DSTDIR/$FNAME.o" "$FNAME.c" || exit 1
		if [ "$3" = "MP" ]; then
			echo "built $FNAME.c"
		fi
		cd "$ORIGDIR" || exit 1
	else
		dirname "$1" >> "$PBFN"
		echo "$CCACHE $CC -c $GCCWARN $DEFS $INCS $CFLAGS -o \"$DSTDIR/$FNAME.o\" \"$FNAME.c\"" >> "$PBFN"
		echo "pbuilt $FNAME.c" >> "$PBFN"
	fi
}

# $1 is directory of .c files to build
# $2 is destination dir
builddir() {
	if [ "$PBFN" = "" ] && [ "$BUILDDIR_MP" -gt 1 ]; then
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

pbuild() {
	if [ "$PBFN" != "" ]; then
		"$UTILDIR/parallel" < "$PBFN" || exit 1
		rm "$PBFN"
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
