
all asan check clean clobber debug distclean maintainer-clean:
	echo running \"gmake ${MAKEFLAGS} $@\"
	gmake ${MAKEFLAGS} $@
