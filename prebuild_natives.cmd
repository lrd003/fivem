@echo off

setlocal EnableDelayedExpansion

set path=C:\msys64\usr\bin;%path%
set LUA_PATH=
set LUA_CPATH=

pacman --noconfirm --needed -Sy make curl diffutils libcurl

pushd ext\native-doc-gen\
sh build.sh
popd

pushd ext\natives\
mkdir inp
curl -z inp\natives_global.lua -Lo inp\natives_global_new.lua http://runtime.fivem.net/doc/natives.lua
curl -z inp\natives_rdr3.lua -Lo inp\natives_rdr3_new.lua http://runtime.fivem.net/doc/natives_rdr_tmp.lua

if exist inp\natives_global.lua (
	diff inp\natives_global.lua inp\natives_global_new.lua > nul
	
	if errorlevel 0 (
		copy /y inp\natives_global_new.lua inp\natives_global.lua
	)
) else (
	copy /y inp\natives_global_new.lua inp\natives_global.lua
)

if exist inp\natives_rdr3.lua (
	diff inp\natives_rdr3.lua inp\natives_rdr3_new.lua > nul
	
	if errorlevel 0 (
		copy /y inp\natives_rdr3_new.lua inp\natives_rdr3.lua
	)
) else (
	copy /y inp\natives_rdr3_new.lua inp\natives_rdr3.lua
)

del inp\natives_global_new.lua
del inp\natives_rdr3_new.lua

make -q

if errorlevel 1 (
	make -j4
	xcopy /y out\*.lua ..\..\data\shared\citizen\scripting\lua
	xcopy /y out\*.js ..\..\data\shared\citizen\scripting\v8
	xcopy /y out\*.d.ts ..\..\data\shared\citizen\scripting\v8
	xcopy /y out\rpc_natives.json ..\..\data\shared\citizen\scripting
	xcopy /y out\*.zip ..\..\data\shared\citizen\scripting\lua

	xcopy /y out\*.cs ..\..\code\client\clrcore
	xcopy /y out\*.h ..\..\code\components\citizen-scripting-lua\src
)

popd
