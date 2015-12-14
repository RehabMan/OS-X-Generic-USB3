# really just some handy scripts...

KEXT=GenericUSBXHCI.kext
DIST=RehabMan-Generic-USB3

VERSION_ERA=$(shell ./print_version.sh)
ifeq "$(VERSION_ERA)" "10.10-"
	INSTDIR=/System/Library/Extensions
else
	INSTDIR=/Library/Extensions
endif

#OPTIONS=LOGNAME=$(LOGNAME)

ifeq ($(findstring 32,$(BITS)),32)
OPTIONS:=$(OPTIONS) -arch i386
endif

ifeq ($(findstring 64,$(BITS)),64)
OPTIONS:=$(OPTIONS) -arch x86_64
endif

INSTALLDIR=Universal
#ifeq ($(OSTYPE),"darwin14")
#INSTALLDIR=Yosemite
#endif

.PHONY: all
all:
	#xcodebuild build $(OPTIONS) -configuration Legacy
	#xcodebuild build $(OPTIONS) -configuration Yosemite
	#xcodebuild build $(OPTIONS) -configuration Mavericks
	xcodebuild build $(OPTIONS) -configuration Universal
	make -f xhcdump.mak

.PHONY: clean
clean:
	#xcodebuild clean $(OPTIONS) -configuration Legacy
	#xcodebuild clean $(OPTIONS) -configuration Yosemite
	#xcodebuild clean $(OPTIONS) -configuration Mavericks
	xcodebuild clean $(OPTIONS) -configuration Universal
	rm ./xhcdump

.PHONY: update_kernelcache
update_kernelcache:
	sudo touch /System/Library/Extensions
	sudo kextcache -update-volume /

.PHONY: install
install:
	sudo cp -R ./Build/$(INSTALLDIR)/$(KEXT) $(INSTDIR)
	make update_kernelcache

.PHONY: distribute
distribute:
	if [ -e ./Distribute ]; then rm -r ./Distribute; fi
	mkdir ./Distribute
	#cp -R ./Build/Legacy ./Distribute
	#cp -R ./Build/Mavericks ./Distribute
	#cp -R ./Build/Yosemite ./Distribute
	cp -R ./Build/Universal ./Distribute
	cp ./xhcdump ./Distribute
	find ./Distribute -path *.DS_Store -delete
	find ./Distribute -path *.dSYM -exec echo rm -r {} \; >/tmp/org.voodoo.rm.dsym.sh
	chmod +x /tmp/org.voodoo.rm.dsym.sh
	/tmp/org.voodoo.rm.dsym.sh
	rm /tmp/org.voodoo.rm.dsym.sh
	ditto -c -k --sequesterRsrc --zlibCompressionLevel 9 ./Distribute ./Archive.zip
	mv ./Archive.zip ./Distribute/`date +$(DIST)-%Y-%m%d.zip`
