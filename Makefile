clean:
	rm -rf $(OBJ_DIR) $(OUT_DIR) $(IR_DIR) android-x86_64 android-arm64 build-protoc

build_protoc:
	cmake -S protobuf -B build-protoc -DCMAKE_BUILD_TYPE=Release -Dprotobuf_BUILD_TESTS=OFF
	cmake --build build-protoc --parallel --target protoc

release_x86_64: build_protoc
	cmake -DANDROID_PLATFORM=21 -DANDROID_ABI=x86_64 -S . -B android-x86_64 -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake -DProtobuf_PROTOC_EXECUTABLE=build-protoc/protoc -DProtobuf_INCLUDE_DIR=protobuf/src -DCMAKE_BUILD_TYPE=Release
	cmake --build android-x86_64 --parallel
	mkdir -p android-x86_64/patches

release_arm64: build_protoc
	cmake -DANDROID_PLATFORM=21 -DANDROID_ABI=arm64-v8a -S . -B android-arm64 -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake -DProtobuf_PROTOC_EXECUTABLE=build-protoc/protoc -DProtobuf_INCLUDE_DIR=protobuf/src -DCMAKE_BUILD_TYPE=Release
	cmake --build android-arm64 --parallel
	mkdir -p android-arm64/patches

release_zips: release_x86_64 release_arm64
	@echo "Creating release zip files..."
	rm -r ./out/mcpelauncher-updates-*-release.zip || true
	cd android-x86_64 && zip -r ../out/mcpelauncher-updates-x86_64-release.zip *.so patches/*
	cd android-arm64 && zip -r ../out/mcpelauncher-updates-arm64-release.zip *.so patches/*
	zip ./out/mcpelauncher-updates-x86_64-release.zip NOTICE.txt
	zip ./out/mcpelauncher-updates-arm64-release.zip NOTICE.txt
	@echo "Release zip files created."
