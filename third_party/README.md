# External dependencies

서버 프로젝트가 다른 Windows PC에서도 같은 입력으로 빌드되도록 확인 가능한 upstream만 사용합니다. `.lib`와 `.dll`은 Git LFS로 관리하며 `git lfs pull` 후 사용할 수 있습니다.

| 구성 요소 | 고정 버전 | 출처 | 저장소 내 위치 |
|---|---|---|---|
| cpp_redis | 4.3.1, commit `bbe38a7f83de943ffcc90271092d689ae02b3489` | [Cylix/cpp_redis](https://github.com/Cylix/cpp_redis/tree/4.3.1) | `include/cpp_redis`, `lib/cpp_redis.lib` |
| tacopie | 3.2.0, commit `26dc81bd77bf3b17a0d75b2d047239869ea2313c` | [Cylix/tacopie](https://github.com/Cylix/tacopie/tree/3.2.0) | `include/tacopie`, `lib/tacopie.lib` |
| MySQL Client | 8.0.44 | [MySQL Community Downloads](https://dev.mysql.com/downloads/mysql/) | `lib/libmysql.lib`, `lib/libmysql.dll` |
| OpenSSL runtime | 3.0.17, MySQL 8.0.44 배포본 포함 파일 | MySQL Community Server 8.0.44 설치 디렉터리 | `lib/libcrypto-3-x64.dll`, `lib/libssl-3-x64.dll` |

cpp_redis와 tacopie는 Visual Studio 2022 x64 Release, `/MD`로 upstream 소스를 직접 빌드했습니다. MySQL import/runtime과 OpenSSL runtime은 `C:\Program Files\MySQL\MySQL Server 8.0`의 설치 파일에서 가져왔습니다. 기존 프로젝트에 있던 출처 불명의 정적 라이브러리는 저장소에 포함하지 않았습니다.

재빌드 예시는 다음과 같습니다. CMake 4.x에서는 오래된 upstream의 최소 정책 때문에 `CMAKE_POLICY_VERSION_MINIMUM=3.5`가 필요합니다.

```powershell
git clone --branch 3.2.0 https://github.com/Cylix/tacopie.git
cmake -S tacopie -B build-tacopie -A x64 '-DMSVC_RUNTIME_LIBRARY_CONFIG=/MD' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
cmake --build build-tacopie --config Release

git clone --branch 4.3.1 https://github.com/Cylix/cpp_redis.git
cmake -S cpp_redis -B build-cpp-redis -A x64 '-DMSVC_RUNTIME_LIBRARY_CONFIG=/MD' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' `
  "-DTACOPIE_INCLUDE_DIR=<tacopie>/includes" "-DTACOPIE_LIBRARY=<build-tacopie>/lib/Release/tacopie.lib"
cmake --build build-cpp-redis --config Release
```

SHA-256은 [MANIFEST.md](MANIFEST.md), 라이선스 전문은 [licenses](licenses)에 있습니다.
