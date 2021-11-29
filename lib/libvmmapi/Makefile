# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	vmmapi
SRCS=	vmmapi.c vmmapi_freebsd.c
INCS=	vmmapi.h

# acpi.c includes acpixf.h which defines some unused parameters in release
# code. Therefore, the option -Wno-unused-parameter is required for compiling.
# Remove it as far as possible.
CWARNFLAGS.vmmapi.c=	-Wno-unused-parameter
CWARNFLAGS.vmmapi_freebsd.c=	-Wno-unused-parameter

LIBADD=	util

CFLAGS+= -I${.CURDIR}
CFLAGS+= -I${SRCTOP}/sys

.include <bsd.lib.mk>
