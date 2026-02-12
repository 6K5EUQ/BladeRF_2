# 빌드 체크리스트 & 트러블슈팅

## 📋 필수 설치 항목 체크리스트

### 1. BladeRF 라이브러리
```bash
# ✓ 설치 확인
pkg-config --modversion libbladerf
# 2.0.0 이상 필요

# ✗ 설치되지 않았으면:
git clone https://github.com/Nuand/bladeRF.git
cd bladeRF/host
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
sudo ldconfig
```

### 2. FFTW3 (Single Precision)
```bash
# ✓ 설치 확인
pkg-config --modversion fftw3f
# 3.3.0 이상 필요

# ✗ 설치되지 않았으면:
sudo apt-get update
sudo apt-get install libfftw3-dev
```

### 3. OpenGL 개발 라이브러리
```bash
# ✓ 설치 확인
glxinfo | head -5

# ✗ 설치되지 않았으면:
sudo apt-get install libgl1-mesa-dev libglew-dev
```

### 4. GLFW3
```bash
# ✓ 설치 확인
pkg-config --modversion glfw3
# 3.0 이상 필요

# ✗ 설치되지 않았으면:
sudo apt-get install libglfw3-dev
```

### 5. C++17 컴파일러
```bash
# ✓ 버전 확인
g++ --version
# 7.0 이상 필요

# 기본 빌드 도구
sudo apt-get install build-essential cmake git
```

---

## 🔨 빌드 단계별 가이드

### 방법 A: CMake (권장)

```bash
# 1. 빌드 디렉토리 생성
mkdir build
cd build

# 2. CMake 구성
cmake ..

# 3. 컴파일
make -j$(nproc)

# 4. 실행
./wideband_sweeper
```

### 방법 B: 수동 컴파일 (빠른 테스트용)

```bash
g++ -O3 -march=native -std=c++17 \
    $(pkg-config --cflags libbladerf fftw3f gl glfw3) \
    wideband_spectrum_sweep.cpp \
    -o wideband_sweeper \
    $(pkg-config --libs libbladerf fftw3f gl glfw3) \
    -lm -lstdc++
```

### 방법 C: 완벽한 디버그 빌드

```bash
g++ -g -O0 -std=c++17 \
    $(pkg-config --cflags libbladerf fftw3f gl glfw3) \
    wideband_spectrum_sweep.cpp \
    -o wideband_sweeper \
    $(pkg-config --libs libbladerf fftw3f gl glfw3) \
    -lm -lstdc++ -Wall -Wextra -Wpedantic
```

---

## 🐛 일반적인 빌드 에러 및 해결

### 에러: "libbladeRF.h: No such file or directory"

**원인**: BladeRF 개발 헤더가 설치되지 않음

**해결**:
```bash
# BladeRF 헤더 경로 확인
find /usr -name "libbladeRF.h" 2>/dev/null

# 없으면 설치
sudo apt-get install libbladerf-dev

# 또는 소스에서 빌드 (권장)
cd bladeRF/host && mkdir build && cd build
cmake .. && make && sudo make install
```

---

### 에러: "undefined reference to `fftwf_plan_dft_1d'"

**원인**: FFTW3f 라이브러리가 링크되지 않음

**해결**:
```bash
# FFTW3f 설치 확인
pkg-config --list-all | grep fftw3f

# 없으면 설치
sudo apt-get install libfftw3-dev

# CMake 재실행
cd build && cmake .. && make
```

---

### 에러: "undefined reference to `glfwCreateWindow'"

**원인**: GLFW3 라이브러리가 없거나 링크되지 않음

**해결**:
```bash
# GLFW3 설치 확인
pkg-config --list-all | grep glfw3

# 없으면 설치
sudo apt-get install libglfw3-dev

# CMake 재실행
cd build && cmake .. && make
```

---

### 에러: "undefined reference to `glClear'"

**원인**: OpenGL 라이브러리 링크 문제

**해결**:
```bash
# OpenGL 설치 확인
dpkg -l | grep libgl

# 없으면 설치
sudo apt-get install libgl1-mesa-dev libglew-dev

# CMake 재실행
cd build && cmake .. && make
```

---

### 에러: "fatal error: GL/glut.h: No such file or directory"

**원인**: GLUT 개발 헤더 누락 (GLFW 기반이지만 필요한 경우)

**해결**:
```bash
sudo apt-get install freeglut3-dev
```

---

### 에러: "error: 'atomic' is not a member of 'std'"

**원인**: C++17이 활성화되지 않음

**해결**:
```bash
# CMakeLists.txt에서 확인
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 또는 수동 컴파일 시:
g++ -std=c++17 ...
```

---

### 에러: "Cannot find package bladerf (required)"

**원인**: CMake가 BladeRF를 찾을 수 없음

**해결**:
```bash
# BladeRF 설치 경로 확인
pkg-config --variable=libdir libbladerf

# 결과가 비어있으면:
sudo ldconfig

# CMake 재실행
cd build && rm -rf * && cmake .. && make
```

---

### 에러: "USB device not found"

**원인**: BladeRF 하드웨어를 인식하지 못함

**해결**:
```bash
# 1. USB 장치 확인
lsusb | grep "Xilinx"

# 2. 커널 메시지 확인
dmesg | tail -20

# 3. udev 규칙 추가
sudo wget -O /etc/udev/rules.d/88-nuand-bladerf.rules \
    https://raw.githubusercontent.com/Nuand/bladeRF/master/host/linux/udev/88-nuand-bladerf.rules
sudo udevadm control --reload

# 4. USB 재연결
# (BladeRF를 뺐다가 다시 연결)

# 5. 권한 확인
ls -la /dev/bus/usb/*/
```

---

## 🧪 빌드 후 테스트

### 1. BladeRF 연결 테스트

```bash
# 컴파일된 프로그램으로 직접 테스트
./wideband_sweeper

# 또는 bladeRF-cli로 사전 테스트
bladerf-cli
> info
> version
```

### 2. 성능 측정

```bash
# CPU 사용률 모니터링 (별도 터미널)
watch -n 1 'ps aux | grep wideband_sweeper'

# 메모리 사용량 모니터링
valgrind --leak-check=full ./wideband_sweeper
# (느리지만 메모리 누수 확인 가능)

# 더 빠른 메모리 모니터링
/usr/bin/time -v ./wideband_sweeper
```

### 3. OpenGL 렌더링 확인

```bash
# 프로그램 실행 후:
# - 윈도우가 뜨고
# - 스펙트럼 라인이 움직이고
# - 워터폴이 아래로 흘러가는지 확인
```

---

## 🛠️ 최적화 빌드

### 최대 성능 빌드

```bash
mkdir build-release
cd build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
./wideband_sweeper
```

### LTO (Link Time Optimization) 활성화

```bash
# CMakeLists.txt 수정
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto")

# 또는 수동 컴파일
g++ -O3 -march=native -flto -std=c++17 ... -fuse-linker-plugin
```

### SIMD 최적화 활성화

```bash
# FFTW3 재빌드 (SIMD 지원)
./configure --enable-sse2 --enable-avx --enable-avx2
make
sudo make install
```

---

## 📊 빌드 출력 예상

### 성공적인 빌드

```
$ cmake ..
-- Found bladeRF: /usr/local/lib/libbladerf.so
-- Found FFTW3F: /usr/lib/x86_64-linux-gnu/libfftw3f.so
-- Found OpenGL: /usr/lib/x86_64-linux-gnu/libGL.so
-- Found GLFW3: /usr/lib/x86_64-linux-gnu/libglfw.so
-- Configuring done
-- Generating done
-- Build files have been written to: /path/to/build

$ make -j4
Scanning dependencies of target wideband_sweeper
[ 20%] Building CXX object CMakeFiles/wideband_sweeper.dir/wideband_spectrum_sweep.cpp.o
[ 40%] Linking CXX executable wideband_sweeper
[100%] Built target wideband_sweeper
```

### 빌드 성공 후

```bash
$ ./wideband_sweeper

╔════════════════════════════════════════════════════════════╗
║         BladeRF Wideband Spectrum Sweeper Setup           ║
╚════════════════════════════════════════════════════════════╝

설정:
  주파수 범위: 47 MHz ~ 6000 MHz
  샘플 레이트: 61.44 MSPS
  청크 개수: 97
  청크당 대역폭: 61.44 MHz
  청크당 시간: 10.31 ms
  FFT 크기: 8192 bins
  주파수 해산도: 7.50 kHz
  워터폴 히스토리: 5.0초 (5행)

╔═══════════════════════════════════════════════════╗
║  BladeRF Wideband Spectrum Sweeper Started       ║
║  Range: 47 MHz ~ 6000 MHz                        ║
...
```

---

## ⚡ 빠른 시작 스크립트

### build.sh

```bash
#!/bin/bash

set -e

echo "=== BladeRF Wideband Spectrum Sweeper Build ==="
echo ""

# 1. 의존성 확인
echo "[1/4] Checking dependencies..."
for pkg in libbladerf fftw3f gl glfw3; do
    if ! pkg-config --exists $pkg; then
        echo "ERROR: $pkg not found. Please install it first."
        exit 1
    fi
done
echo "✓ All dependencies found"

# 2. 빌드 디렉토리 준비
echo "[2/4] Preparing build directory..."
mkdir -p build
cd build

# 3. CMake 구성
echo "[3/4] Configuring with CMake..."
cmake ..

# 4. 컴파일
echo "[4/4] Compiling..."
make -j$(nproc)

cd ..

echo ""
echo "✓ Build complete!"
echo ""
echo "To run:"
echo "  ./build/wideband_sweeper"
```

### run.sh

```bash
#!/bin/bash

if [ ! -f build/wideband_sweeper ]; then
    echo "ERROR: Executable not found. Run ./build.sh first."
    exit 1
fi

echo "Starting BladeRF Wideband Spectrum Sweeper..."
echo ""
echo "Controls:"
echo "  W/S: Adjust dB max"
echo "  A/D: Adjust dB min"
echo "  R: Reset all"
echo "  ESC: Exit"
echo ""

./build/wideband_sweeper
```

---

## 📝 추가 정보

### 시스템 요구사항

- **OS**: Linux (Ubuntu 18.04+, Debian 10+)
- **CPU**: Multi-core (i7/Ryzen 5 이상 권장)
- **RAM**: 4GB 최소, 8GB 권장
- **GPU**: OpenGL 4.0+ 지원 (통합 그래픽도 가능)
- **USB**: USB 3.0 포트

### 지원 BladeRF 모델

- BladeRF 2.0 micro (xA4)
- BladeRF 2.0 micro (xA9) ✓ (본 프로젝트)
- BladeRF 2.0 (전체 크기)

### 호환 컴파일러

- GCC 7.0+
- Clang 5.0+
- ICC (Intel C++ Compiler)

---

**마지막 업데이트**: 2026-02-11  
**작성자**: DSA Project Team
