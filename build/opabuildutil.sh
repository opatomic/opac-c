
function handleErr {
	SNAME=$(basename $BASH_SOURCE)
	echo "$SNAME: Error on line $1"
	exit 1
}

trap 'handleErr $LINENO' ERR


CC="${CC:-cc}"
AR="${AR:-ar}"
ARFLAGS="${ARFLAGS:--rcs}"
TGTOS="${TGTOS:-$(uname | tr '[:upper:]' '[:lower:]')}"

UNAME="${UNAME:-$(uname | tr '[:upper:]' '[:lower:]')}"
if [[ "$UNAME" = "darwin" ]]; then
	NPROC="${NPROC:-$(sysctl -n hw.logicalcpu)}"
else
	NPROC="${NPROC:-$(nproc --all 2>/dev/null)}"
fi

if [[ -z "$BUILDDIR_MP" && $NPROC -gt 1 ]]; then
	BUILDDIR_MP="${BUILDDIR_MP:-$(($NPROC*4))}"
fi

function testgccopt {
	($CC -E -Werror "$1" - < /dev/null &> /dev/null && echo "$1") || echo ""
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
if [[ `uname | tr '[:upper:]' '[:lower:]'` == "darwin" ]]; then
	STRIPALLFLAG=""
fi

# $1 is .c file
# $2 is destination directory for .o file
function buildcfile {
	trap 'handleErr $LINENO' ERR

	local FNAME=`basename $1 .c`
	# cd to directory containing .c file so that __FILE__ does not contain directories
	pushd "`dirname $1`" > /dev/null
	if [[ "$3" != "MP" ]]; then
		echo "building $FNAME.c"
	fi

	$CC -c $GCCOPTS $GCCWARN $DEFS $INCS $CFLAGS -o "$2/$FNAME.o" "$FNAME.c"
	#if [[ $? -ne 0 ]]; then
	#	exit 1
	#fi
	if [[ "$3" = "MP" ]]; then
		echo "built $FNAME.c"
	fi
	popd > /dev/null
}

#function buildcfile {
#	if [[ "$(jobs -p | wc -l)" -ge $NPROC ]]; then
#		wait -n
#	fi
#	buildcfile2 $1 $2 "MP" &
#}

# $1 is directory of .c files to build
# $2 is destination dir
function builddir {
	trap 'handleErr $LINENO' ERR

	if [[ "$BUILDDIR_MP" -gt 1 ]]; then
		local i=0
		for fname in `ls $1/*.c`; do
			i=$(($i+1))
			buildcfile $fname $2 "MP" &
			if [[ $(($i % $BUILDDIR_MP)) -eq 0 ]]; then
				wait
			fi
		done
		wait
		# determine whether any file failed to build
		for i in `ls $1/*.c`; do
			if [[ ! ( -f "$2/$(basename $i .c).o" ) ]]; then
				echo "error occurred; exiting"
				exit 1
			fi
		done
	else
		for i in `ls $1/*.c`; do
			buildcfile $i $2
		done
	fi
}

function deldir {
	if [[ -d "$1" ]]; then
		rm -rf "$1"
	fi
}

function cleandir {
	deldir "$1"
	mkdir "$1"
}

function ensuredir {
	if [[ ! -d "$1" ]]; then
		mkdir "$1"
	fi
}

