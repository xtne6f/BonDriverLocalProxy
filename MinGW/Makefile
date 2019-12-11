all: cp_dep BonDriver_Proxy.dll BonDriverLocalProxy.exe
clean: BonDriver_Proxy.dll.clean BonDriverLocalProxy.exe.clean rm_dep
BonDriver_Proxy.dll: ../BonDriver_Proxy/BonDriver_Proxy.cpp
	$(CXX) -Wall -Wl,-s -Wl,--dynamicbase -Wl,--nxcompat -static-libgcc -static-libstdc++ -shared -DNDEBUG -D_WINDOWS -D_UNICODE -DUNICODE -O2 -o $@ $<
BonDriverLocalProxy.exe: ../BonDriverLocalProxy/BonDriverLocalProxy.cpp
	$(CXX) -Wall -mwindows -Wl,-s -Wl,--dynamicbase -Wl,--nxcompat -static-libgcc -static-libstdc++ -DNDEBUG -D_WINDOWS -D_UNICODE -DUNICODE -O2 -o $@ $< -lole32
BonDriver_Proxy.dll.clean:
	$(RM) $(basename $@)
BonDriverLocalProxy.exe.clean:
	$(RM) $(basename $@)
cp_dep:
	cp -n $(MINGW_PREFIX)/bin/libwinpthread-1.dll .
rm_dep:
	$(RM) libwinpthread-1.dll
