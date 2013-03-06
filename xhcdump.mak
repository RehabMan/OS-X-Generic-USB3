CC:=clang
TARGET_ARCH:=-m64
CFLAGS:=-fvisibility=hidden -fno-stack-protector -mmacosx-version-min=10.5 -Wall -Wextra
LDFLAGS:=-framework IOKit -Xlinker -no_function_starts -Xlinker -no_version_load_command -Xlinker -no_data_in_code_info -Xlinker -no_uuid
xhcdump: xhcdump.c
	$(LINK.c) -o $@ $^
	/usr/bin/strip -x $@
