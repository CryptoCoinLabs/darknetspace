cd darknetspace
if [ $? -ne 0 ]; then
    echo "Failed to cd darknetspace"
    exit $?
fi

git pull
if [ $? -ne 0 ]; then
    echo "Failed to git pull"
    exit $?
fi

rm -rf build; mkdir -p build/release; 

cd build/release;

cmake -D BUILD_GUI=TRUE -D CMAKE_PREFIX_PATH=/Users/phil/Qt/5.3/clang_64 -D CMAKE_BUILD_TYPE=Release ../..
if [ $? -ne 0 ]; then
    echo "Failed to cmake"
    exit $?
fi



make qt-dnsp
if [ $? -ne 0 ]; then
    echo "Failed to make qt-dnsp"
    exit $?
fi


cd src/
if [ $? -ne 0 ]; then
    echo "Failed to cd src"
    exit $?
fi

/Users/phil/Qt/5.3/clang_64/bin/macdeployqt qt-dnsp.app
if [ $? -ne 0 ]; then
    echo "Failed to macdeployqt qt-boolb.app"
    exit $?
fi

cp -R ../../../src/gui/qt-daemon/html qt-dnsp.app/Contents/MacOS
if [ $? -ne 0 ]; then
    echo "Failed to cp html to MacOS"
    exit $?
fi

cp ../../../src/gui/qt-daemon/app.icns qt-dnsp.app/Contents/Resources
if [ $? -ne 0 ]; then
    echo "Failed to cp app.icns to resources"
    exit $?
fi

zip -r -y "dnsp-macos-x64-v0.2.0.zip" qt-dnsp.app
if [ $? -ne 0 ]; then
    echo "Failed to zip app"
    exit $?
fi

cd ../..
echo "Build success"

