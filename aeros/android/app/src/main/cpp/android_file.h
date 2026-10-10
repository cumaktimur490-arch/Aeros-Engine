#pragma once
// Android file handling — загрузка STL из assets и external storage
// Оптимизировано для слабых телефонов: минимум копий, streaming

#include <android/asset_manager.h>
#include <string>
#include <vector>

void androidFileInit(AAssetManager* assetManager);
bool androidFileExists(const char* path);
std::vector<char> androidFileLoad(const char* path);
std::string androidGetExternalFilesDir();
std::string androidGetCacheDir();
std::vector<std::string> androidListAssetModels();
bool androidCopyAssetToCache(const char* assetPath, const char* cachePath);
