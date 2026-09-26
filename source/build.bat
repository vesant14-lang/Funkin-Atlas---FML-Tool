@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
call "%ROOT%setup_msvc.bat" || exit /b 1
set "SDL=%ROOT%third_party\SDL3-3.4.14"
set "IMGUI=%ROOT%third_party\imgui"
set "CFLAGS=/nologo /c /O2 /EHsc /std:c++17 /W3 /wd4996 /wd4267 /wd4244 /bigobj"
set "IMFLAGS=%CFLAGS% /I"%SDL%\include" /I"%IMGUI%""
set "ATLAS_EXE=FunkinAtlas.exe"
if /i "%~1"=="check" set "ATLAS_EXE=FunkinAtlas-check.exe"
if /i "%~1"=="check" if defined FML_ATLAS_CHECK_EXE set "ATLAS_EXE=%FML_ATLAS_CHECK_EXE%"
if not exist "%ROOT%build" mkdir "%ROOT%build" || exit /b 1
pushd "%ROOT%build" || exit /b 1
cl %CFLAGS% "%ROOT%third_party\pugixml.cpp" /Fo:pugixml.obj || goto :fail
cl %CFLAGS% /TC "%ROOT%third_party\miniz\miniz.c" /Fo:miniz.obj || goto :fail
cl %CFLAGS% /TC "%ROOT%third_party\miniz\miniz_tdef.c" /Fo:miniz_tdef.obj || goto :fail
cl %CFLAGS% /TC "%ROOT%third_party\miniz\miniz_tinfl.c" /Fo:miniz_tinfl.obj || goto :fail
cl %CFLAGS% /TC "%ROOT%third_party\miniz\miniz_zip.c" /Fo:miniz_zip.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_io\Vfs.cpp" /Fo:Vfs.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_adapters\SpriteResolver.cpp" /Fo:SpriteResolver.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_core\Hash.cpp" /Fo:Hash.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\CodenameXml.cpp" /Fo:CodenameXml.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\SparrowAtlas.cpp" /Fo:SparrowAtlas.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\AnimateAtlas.cpp" /Fo:AnimateAtlas.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\LegacyChart.cpp" /Fo:LegacyChart.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\CodenameChart.cpp" /Fo:CodenameChart.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\SongMeta.cpp" /Fo:SongMeta.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\ChartExchange.cpp" /Fo:ChartExchange.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\VSliceExport.cpp" /Fo:VSliceExport.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\PsychCharacter.cpp" /Fo:PsychCharacter.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\LuaAst.cpp" /Fo:LuaAst.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\LuaStatic.cpp" /Fo:LuaStatic.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_formats\PsychStage.cpp" /Fo:PsychStage.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_runtime\Scene.cpp" /Fo:Scene.obj || goto :fail
cl %IMFLAGS% "%ROOT%third_party\imgui\imgui.cpp" /Fo:imgui.obj || goto :fail
cl %IMFLAGS% "%ROOT%third_party\imgui\imgui_draw.cpp" /Fo:imgui_draw.obj || goto :fail
cl %IMFLAGS% "%ROOT%third_party\imgui\imgui_tables.cpp" /Fo:imgui_tables.obj || goto :fail
cl %IMFLAGS% "%ROOT%third_party\imgui\imgui_widgets.cpp" /Fo:imgui_widgets.obj || goto :fail
cl %IMFLAGS% "%ROOT%third_party\imgui\imgui_impl_sdl3.cpp" /Fo:imgui_impl_sdl3.obj || goto :fail
cl %IMFLAGS% "%ROOT%third_party\imgui\imgui_impl_opengl3.cpp" /Fo:imgui_impl_gl3.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_app\ModExplorerCatalog.cpp" /Fo:ModExplorerCatalog.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_app\AnimationGif.cpp" /Fo:AnimationGif.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_app\GifWriter.cpp" /Fo:GifWriter.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_render\FxShaders.cpp" /Fo:FxShaders.obj || goto :fail
cl %IMFLAGS% "%ROOT%src\fml_render\GlRenderer.cpp" /Fo:GlRenderer.obj || goto :fail
cl %IMFLAGS% "%ROOT%src\fml_render\ShaderLibrary.cpp" /Fo:ShaderLibrary.obj || goto :fail
cl %CFLAGS% /wd4245 /wd4456 /wd4457 /wd4701 "%ROOT%src\fml_render\TextRaster.cpp" /Fo:TextRaster.obj || goto :fail
cl %CFLAGS% "%ROOT%src\fml_audio\AudioEngine.cpp" /Fo:AudioEngine.obj || goto :fail
cl %CFLAGS% /TC "%ROOT%src\tools\stb_vorbis_impl.c" /Fo:stb_vorbis_impl.obj || goto :fail
cl %IMFLAGS% "%ROOT%ModExplorer\src\main.cpp" /Fo:FunkinAtlas.obj || goto :fail
rc /nologo /I "%ROOT%ModExplorer\assets" /fo FunkinAtlas.res "%ROOT%ModExplorer\assets\app.rc" || goto :fail
link /nologo FunkinAtlas.obj FunkinAtlas.res ModExplorerCatalog.obj AnimationGif.obj GifWriter.obj GlRenderer.obj ShaderLibrary.obj TextRaster.obj FxShaders.obj CodenameXml.obj PsychCharacter.obj PsychStage.obj VSliceExport.obj LuaStatic.obj LuaAst.obj SpriteResolver.obj Scene.obj SparrowAtlas.obj AnimateAtlas.obj Vfs.obj Hash.obj LegacyChart.obj CodenameChart.obj SongMeta.obj ChartExchange.obj AudioEngine.obj stb_vorbis_impl.obj miniz.obj miniz_tdef.obj miniz_tinfl.obj miniz_zip.obj pugixml.obj imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj imgui_impl_sdl3.obj imgui_impl_gl3.obj "%SDL%\lib\x64\SDL3.lib" opengl32.lib shell32.lib ole32.lib /OUT:"%ROOT%%ATLAS_EXE%" || goto :fail
copy /y "%SDL%\lib\x64\SDL3.dll" "%ROOT%SDL3.dll" >nul || goto :fail
popd
echo [OK] %ATLAS_EXE%
exit /b 0
:fail
popd
echo [ERROR] Funkin Atlas build failed.
exit /b 1
