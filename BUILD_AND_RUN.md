# BladeRF Wideband Spectrum Sweeper - 빌드 및 실행 가이드

## 개요

이 프로그램은 BladeRF xA9를 이용하여 **47 MHz ~ 6 GHz 전체 대역**을 **1초마다 완전히 스윕**하면서 광대역 스펙트럼을 실시간 시각화하는 애플리케이션입니다.

## 주요 특징

- **광대역 스펙트럼 스윕**: 47 MHz ~ 6 GHz를 약 97개 청크로 나누어 1초마다 완전히 스캔
- **고속 FFT 처리**: 8192-bin FFT로 약 7.5 kHz 해상도 달성
- **실시간 워터폴**: 5초 분량의 시간 축 스펙트럼 디스플레이
- **OpenGL 가속 렌더링**: 부드러운 스펙트럼 및 워터폴 시각화

## 필수 의존성

### 1. BladeRF 라이브러리
```bash
# 이미 설치되어 있다면 스킵
sudo apt-get install libbladerf-dev

# 또는 소스에서 빌드 (권장)
git clone https://github.com/Nuand/bladeRF.git
cd bladeRF/host
mkdir build && cd build
cmake ..
make
sudo make install
sudo ldconfig
```

### 2. FFTW3 (Single Precision)
```bash
sudo apt-get install libfftw3-dev
```

### 3. OpenGL 및 GLFW3
```bash
sudo apt-get install libglfw3-dev libglew-dev
```

## 빌드

### 방법 1: CMake (권장)

```bash
mkdir build
cd build
cmake ..
make -j4
```

### 방법 2: 수동 컴파일

```bash
g++ -O3 -march=native \
    -I/usr/local/include \
    -L/usr/local/lib \
    wideband_spectrum_sweep.cpp \
    -o wideband_sweeper \
    -lbladerf -lfftw3f -lGL -lglfw -lm -lstdc++
```

## 실행

```bash
./wideband_sweeper
```

## 키 조작

| 키 | 기능 |
|---|---|
| **W** | dB 최대값 증가 (+5 dB) |
| **S** | dB 최대값 감소 (-5 dB) |
| **A** | dB 최소값 감소 (-5 dB) |
| **D** | dB 최소값 증가 (+5 dB) |
| **UP** | 확대 (1.1배) |
| **DOWN** | 축소 (1.1배) |
| **R** | 모든 설정 리셋 |
| **ESC** | 프로그램 종료 |

## 동작 원리

### 1. 주파수 청크 분할
- **전체 범위**: 47 MHz ~ 6000 MHz (5953 MHz)
- **샘플 레이트**: 61.44 MSPS (실제 대역폭 ~61.44 MHz)
- **청크 개수**: ceil(5953 / 61.44) = **97개**

### 2. 스윕 타이밍
- **전체 스윕 주기**: 1초
- **청크당 시간**: 1000 ms / 97 = **약 10.3 ms**
- **샘플 수/청크**: 61.44 MSPS × 0.0103s = **약 633,000 샘플**

### 3. FFT 처리
- **FFT 크기**: 8192 bins
- **주파수 해상도**: 61.44 MHz / 8192 = **약 7.5 kHz/bin**
- **처리 시간**: ~2-3 ms (FFTW 최적화)

### 4. 워터폴 누적
- **히스토리**: 5초
- **행 개수**: 5 / 1 = **5행** (스윕당 1행 추가)
- **메모리**: ~97 청크 × 8192 bins × 4 bytes × 5행 ≈ 15.9 MB

## 성능 지표

| 항목 | 값 |
|---|---|
| 주파수 대역폭 | 5953 MHz (47 MHz ~ 6 GHz) |
| 스윕 주기 | 1초 |
| 주파수 해상도 | 7.5 kHz |
| 메모리 사용 | ~20-25 MB |
| CPU 사용률 | 보통 (멀티스레드 최적화) |

## 문제 해결

### 1. BladeRF 장치 인식 안됨
```bash
lsusb  # BladeRF 디바이스 확인
sudo dmesg | tail -20  # 커널 메시지 확인
```

### 2. FFTW 링크 에러
```bash
pkg-config --cflags --libs fftw3f
# 이 결과를 컴파일 명령에 추가
```

### 3. OpenGL 에러
```bash
glxinfo | grep "direct rendering"  # OpenGL 가속 확인
```

### 4. 낮은 프레임 레이트
- dB 범위 (W/S/A/D)를 넓혀서 렌더링 복잡도 줄이기
- 화면 해상도를 낮추기

## 향후 개선

- [ ] 주파수 영역 줌 및 팬 기능
- [ ] 특정 주파수 대역 녹음
- [ ] FFT 크기 동적 조정
- [ ] 스윕 속도 사용자 설정
- [ ] 신호 감지 및 경보 기능
- [ ] 스펙트럼 데이터 파일 저장
- [ ] 네트워크 스트리밍

## 추가 정보

- BladeRF 공식 문서: https://github.com/Nuand/bladeRF
- FFTW 문서: http://www.fftw.org/
- GLFW 문서: https://www.glfw.org/

---

**작성**: DSA Project  
**마지막 업데이트**: 2026-02-11
