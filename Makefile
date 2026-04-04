PLAYFAB_ZIP = PlayFabMultiplayer.AndroidRelease.zip
PLAYFAB_URL = https://github.com/PlayFab/PlayFabMultiplayer/releases/download/v1.9.0/Microsoft.PlayFab.PlayFabMultiplayer.Cpp.AndroidRelease.zip
PLAYFAB_DIR = PlayFabMultiplayer

clean:
	rm -rf out android-x86_64 android-arm64 build-protoc

$(PLAYFAB_ZIP):
	@echo "Downloading PlayFab ZIP..."
	@curl -L -o $(PLAYFAB_ZIP) $(PLAYFAB_URL)

.PHONY: playfab
playfab: $(PLAYFAB_ZIP)
	@echo "Extracting PlayFab..."
	@rm -rf $(PLAYFAB_DIR)
	@unzip -q $(PLAYFAB_ZIP) -d $(PLAYFAB_DIR)

	@echo "Removing old arm64-v8a from patches..."
	@rm -rf android-arm64/patches
	@mkdir android-arm64/patches

	@echo "Removing old x86_64 from patches..."
	@rm -rf android-x86_64/patches
	@mkdir android-x86_64/patches

	@echo "Copying new arm64-v8a..."
	@cp $(PLAYFAB_DIR)/bin/arm64-v8a/* android-arm64/patches/

	@echo "Copying new arm64-v8a..."
	@cp $(PLAYFAB_DIR)/bin/x86_64/* android-x86_64/patches/

	@echo "PlayFab setup complete."


build_protoc:
	cmake -S protobuf -B build-protoc -DCMAKE_BUILD_TYPE=Release -Dprotobuf_BUILD_TESTS=OFF
	cmake --build build-protoc --parallel --target protoc

release_x86_64: build_protoc
	cmake -DANDROID_PLATFORM=21 -DANDROID_ABI=x86_64 -S . -B android-x86_64 -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake -DProtobuf_PROTOC_EXECUTABLE=build-protoc/protoc -DProtobuf_INCLUDE_DIR=protobuf/src -DCMAKE_BUILD_TYPE=Release
	cmake --build android-x86_64 --parallel

release_arm64: build_protoc
	cmake -DANDROID_PLATFORM=21 -DANDROID_ABI=arm64-v8a -S . -B android-arm64 -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake -DProtobuf_PROTOC_EXECUTABLE=build-protoc/protoc -DProtobuf_INCLUDE_DIR=protobuf/src -DCMAKE_BUILD_TYPE=Release
	cmake --build android-arm64 --parallel

release_zips: release_x86_64 release_arm64 playfab
	@echo "Creating release zip files..."
	mkdir -p out
	rm -r ./out/mcpelauncher-updates-*-release.zip || true
	cd android-x86_64 && zip -r ../out/mcpelauncher-updates-x86_64-release.zip *.so patches/*
	cd android-arm64 && zip -r ../out/mcpelauncher-updates-arm64-release.zip *.so patches/*
	zip ./out/mcpelauncher-updates-x86_64-release.zip NOTICE.txt
	zip ./out/mcpelauncher-updates-arm64-release.zip NOTICE.txt
	@echo "Release zip files created."
