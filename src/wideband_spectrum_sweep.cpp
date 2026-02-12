#include <libbladeRF.h>
#include <fftw3.h>
#include <GLFW/glfw3.h>
#include <GL/freeglut.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <unistd.h>

// ==================== 설정 상수 ====================
#define FFT_SIZE              8192
#define RX_GAIN               30        // 40 → 30으로 조정
#define CHANNEL               BLADERF_CHANNEL_RX(0)
#define SAMPLE_RATE           61440000  // 61.44 MSPS
#define START_FREQ_MHZ        80
#define END_FREQ_MHZ          110
#define STEP_SIZE_MHZ         50        // 50 MHz 단계
#define WATERFALL_HISTORY     20       // 워터폴 히스토리 라인 수

// ==================== 전역 상태 ====================
struct WidebandState {
    std::atomic<bool> running{true};
    std::mutex mutex;
    
    // 스펙트럼 데이터
    std::vector<float> full_spectrum;      // 현재 스펙트럼
    std::vector<float> peak_spectrum;      // Peak hold
    std::vector<float> avg_spectrum_acc;   // 평균 누적
    std::deque<std::vector<float>> waterfall_history;
    uint64_t start_freq;
    uint64_t end_freq;
    uint64_t current_freq;
    int num_chunks;
    int sweep_count;
    // 평균화 설정
    float avg_alpha;  // Exponential averaging factor (0.0 ~ 1.0)
    bool peak_hold_enabled;
    
    // dB 범위 조정
    float db_min;
    float db_max;
    bool adjust_mode;
    
    // FFT 관련
    fftw_complex* fft_in;
    fftw_complex* fft_out;
    fftw_plan fft_plan;
    std::vector<float> window;
    
    WidebandState() {
        start_freq = START_FREQ_MHZ * 1000000ULL;
        end_freq = END_FREQ_MHZ * 1000000ULL;
        current_freq = start_freq;
        num_chunks = 1;  // 2 → 1로 줄임 (평균화 감소)
        sweep_count = 0;
        
        // 평균화 설정
        avg_alpha = 0.3f;  // 0.3 = 새 데이터 30%, 이전 70%
        peak_hold_enabled = false;  // true → false (노란색 선 제거)
        
        // dB 범위 기본값
        db_min = -80.0f;   // -100 → -80
        db_max = -10.0f;   // -30 → -10
        adjust_mode = false;
        
        // 스펙트럼 배열 초기화
        // 양쪽으로 여유 공간 추가 (±SAMPLE_RATE/2)
        uint64_t total_bandwidth = end_freq - start_freq;
        uint64_t extended_bandwidth = total_bandwidth + SAMPLE_RATE;  // 양쪽 확장
        size_t total_bins = (extended_bandwidth / (SAMPLE_RATE / FFT_SIZE)) + FFT_SIZE;
        full_spectrum.resize(total_bins, -80.0f);
        peak_spectrum.resize(total_bins, -120.0f);
        avg_spectrum_acc.resize(total_bins, -80.0f);
        
        // FFT 초기화
        fft_in = fftw_alloc_complex(FFT_SIZE);
        fft_out = fftw_alloc_complex(FFT_SIZE);
        fft_plan = fftw_plan_dft_1d(FFT_SIZE, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
        
        // Hann 윈도우 생성
        window.resize(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; i++) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
        }
    }
    
    ~WidebandState() {
        fftw_destroy_plan(fft_plan);
        fftw_free(fft_in);
        fftw_free(fft_out);
    }
    
    void add_waterfall_line() {
        waterfall_history.push_back(full_spectrum);
        if (waterfall_history.size() > WATERFALL_HISTORY) {
            waterfall_history.pop_front();
        }
    }
};

static WidebandState wideband_state;

// OpenGL 관련
static GLFWwindow* window = nullptr;
static int window_width = 1920;
static int window_height = 1080;

// ==================== 색상 맵 ====================
void value_to_color(float value, float min_val, float max_val, float& r, float& g, float& b) {
    float normalized = (value - min_val) / (max_val - min_val);
    normalized = fmaxf(0.0f, fminf(1.0f, normalized));
    
    // Jet colormap - 더 선명하게
    if (normalized < 0.25f) {
        r = 0.0f;
        g = normalized * 4.0f;
        b = 1.0f;
    } else if (normalized < 0.5f) {
        r = 0.0f;
        g = 1.0f;
        b = 1.0f - (normalized - 0.25f) * 4.0f;
    } else if (normalized < 0.75f) {
        r = (normalized - 0.5f) * 4.0f;
        g = 1.0f;
        b = 0.0f;
    } else {
        r = 1.0f;
        g = 1.0f - (normalized - 0.75f) * 4.0f;
        b = 0.0f;
    }
}

// ==================== 텍스트 렌더링 ====================
void draw_text_gl(float x, float y, const char* text) {
    glRasterPos2f(x, y);
    for (const char* c = text; *c != '\0'; c++) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
    }
}

// ==================== FFT 처리 ====================
void process_fft(const int16_t* iq_data, std::vector<float>& fft_result) {
    // IQ 데이터를 복소수로 변환하고 윈도우 적용
    for (int i = 0; i < FFT_SIZE; i++) {
        float i_val = iq_data[2 * i] / 2048.0f;  // Q11 → 정규화
        float q_val = iq_data[2 * i + 1] / 2048.0f;
        wideband_state.fft_in[i][0] = i_val * wideband_state.window[i];
        wideband_state.fft_in[i][1] = q_val * wideband_state.window[i];
    }
    
    // FFT 수행
    fftw_execute(wideband_state.fft_plan);
    
    // 파워 스펙트럼 계산 (dBFS)
    fft_result.resize(FFT_SIZE);
    
    // 윈도우 보정 계산
    float window_power_sum = 0.0f;
    for (int i = 0; i < FFT_SIZE; i++) {
        window_power_sum += wideband_state.window[i] * wideband_state.window[i];
    }
    float window_correction = 10.0f * log10f(window_power_sum / FFT_SIZE);
    
    for (int i = 0; i < FFT_SIZE; i++) {
        float real = wideband_state.fft_out[i][0];
        float imag = wideband_state.fft_out[i][1];
        float power = (real * real + imag * imag) / (FFT_SIZE * FFT_SIZE);
        
        // dBFS로 변환 (Full Scale 기준)
        float db = 10.0f * log10f(power + 1e-20f);
        
        // 윈도우 손실 보정
        db -= window_correction;
        
        fft_result[i] = db;
    }
    
    // FFT shift (DC를 중앙으로)
    std::vector<float> shifted(FFT_SIZE);
    int half = FFT_SIZE / 2;
    for (int i = 0; i < half; i++) {
        shifted[i] = fft_result[i + half];
        shifted[i + half] = fft_result[i];
    }
    fft_result = shifted;
}

// ==================== BladeRF 스윕 스레드 ====================
void bladerf_sweep_thread() {
    struct bladerf *dev = nullptr;
    int status;
    
    printf("\n🚀 BladeRF 스펙트럼 스위퍼 시작\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    // BladeRF 열기
    status = bladerf_open(&dev, nullptr);
    if (status != 0) {
        fprintf(stderr, "❌ BladeRF 열기 실패: %s\n", bladerf_strerror(status));
        wideband_state.running = false;
        return;
    }
    printf("✓ BladeRF 연결됨\n");
    
    // 샘플 레이트 설정
    uint32_t actual_rate;
    status = bladerf_set_sample_rate(dev, CHANNEL, SAMPLE_RATE, &actual_rate);
    if (status != 0) {
        fprintf(stderr, "❌ 샘플 레이트 설정 실패: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        wideband_state.running = false;
        return;
    }
    printf("✓ 샘플 레이트: %.2f MSPS\n", actual_rate / 1e6);
    
    // 대역폭 설정
    uint32_t actual_bw;
    status = bladerf_set_bandwidth(dev, CHANNEL, actual_rate, &actual_bw);
    if (status != 0) {
        fprintf(stderr, "❌ 대역폭 설정 실패: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        wideband_state.running = false;
        return;
    }
    printf("✓ 대역폭: %.2f MHz\n", actual_bw / 1e6);
    
    // 게인 설정
    status = bladerf_set_gain_mode(dev, CHANNEL, BLADERF_GAIN_MANUAL);
    if (status != 0) {
        fprintf(stderr, "❌ 게인 모드 설정 실패: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        wideband_state.running = false;
        return;
    }
    
    status = bladerf_set_gain(dev, CHANNEL, RX_GAIN);
    if (status != 0) {
        fprintf(stderr, "❌ 게인 설정 실패: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        wideband_state.running = false;
        return;
    }
    printf("✓ RX 게인: %d dB\n", RX_GAIN);
    
    // 동기 모드 설정
    status = bladerf_sync_config(dev, BLADERF_RX_X1, BLADERF_FORMAT_SC16_Q11,
                                 512, 16384, 128, 3000);
    if (status != 0) {
        fprintf(stderr, "❌ 동기 설정 실패: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        wideband_state.running = false;
        return;
    }
    
    // RX 활성화
    status = bladerf_enable_module(dev, CHANNEL, true);
    if (status != 0) {
        fprintf(stderr, "❌ RX 활성화 실패: %s\n", bladerf_strerror(status));
        bladerf_close(dev);
        wideband_state.running = false;
        return;
    }
    printf("✓ RX 모듈 활성화됨\n");
    
    usleep(200000);
    
    // IQ 버퍼
    std::vector<int16_t> iq_buffer(FFT_SIZE * 2 * wideband_state.num_chunks);
    std::vector<float> fft_result;
    
    printf("\n📡 스펙트럼 스윕 시작...\n");
    printf("  범위: %llu MHz ~ %llu MHz\n", 
           wideband_state.start_freq / 1000000,
           wideband_state.end_freq / 1000000);
    printf("  FFT 크기: %d\n", FFT_SIZE);
    printf("  청크 수: %d\n", wideband_state.num_chunks);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("키 바인딩:\n");
    printf("  F        : dB 범위 조정 모드 토글\n");
    printf("  ↑/↓      : dB 최댓값 조정 (F 모드 시)\n");
    printf("  ←/→      : dB 최솟값 조정 (F 모드 시)\n");
    printf("  R        : dB 범위 리셋\n");
    printf("  ESC      : 종료\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
    
    // 메인 스윕 루프
    while (wideband_state.running) {
        wideband_state.sweep_count++;
        
        uint64_t freq = wideband_state.start_freq;
        int step_count = 0;
        
        printf("\n=== SWEEP #%d START ===\n", wideband_state.sweep_count);
        
        // 🔴 새 스윕 시작: 스펙트럼 데이터 초기화 (과거 주파수 데이터 제거)
        {
            std::lock_guard<std::mutex> lock(wideband_state.mutex);
            std::fill(wideband_state.full_spectrum.begin(), 
                     wideband_state.full_spectrum.end(), -80.0f);
            std::fill(wideband_state.peak_spectrum.begin(), 
                     wideband_state.peak_spectrum.end(), -120.0f);
            std::fill(wideband_state.avg_spectrum_acc.begin(), 
                     wideband_state.avg_spectrum_acc.end(), -80.0f);
            wideband_state.waterfall_history.clear();
        }
        printf("✓ 스펙트럼 데이터 초기화 완료 (과거 데이터 제거)\n");
        
        while (freq <= wideband_state.end_freq && wideband_state.running) {
            step_count++;
            
            // 주파수 설정
            status = bladerf_set_frequency(dev, CHANNEL, freq);
            if (status != 0) {
                fprintf(stderr, "\n❌ 주파수 설정 실패: %s\n", bladerf_strerror(status));
                break;
            }
            
            wideband_state.current_freq = freq;
            
            // 정착 시간
            usleep(1000);
            
            // 여러 청크 수집 및 평균화
            std::vector<float> avg_spectrum(FFT_SIZE, 0.0f);
            
            for (int chunk = 0; chunk < wideband_state.num_chunks; chunk++) {
                // IQ 데이터 수신
                status = bladerf_sync_rx(dev, iq_buffer.data() + (chunk * FFT_SIZE * 2),
                                        FFT_SIZE, nullptr, 5000);
                if (status != 0) {
                    fprintf(stderr, "\n❌ RX 오류: %s\n", bladerf_strerror(status));
                    break;
                }
                
                // FFT 처리
                process_fft(iq_buffer.data() + (chunk * FFT_SIZE * 2), fft_result);
                
                // 누적
                for (size_t i = 0; i < fft_result.size(); i++) {
                    avg_spectrum[i] += fft_result[i];
                }
            }
            
            // 평균 계산
            for (size_t i = 0; i < avg_spectrum.size(); i++) {
                avg_spectrum[i] /= wideband_state.num_chunks;
            }
            
            // 디버그: 평균 파워 출력
            float avg_power = 0.0f;
            float max_power = -200.0f;
            float min_power = 200.0f;
            for (size_t i = 0; i < avg_spectrum.size(); i++) {
                avg_power += avg_spectrum[i];
                if (avg_spectrum[i] > max_power) max_power = avg_spectrum[i];
                if (avg_spectrum[i] < min_power) min_power = avg_spectrum[i];
            }
            avg_power /= avg_spectrum.size();
            
            printf("Step %d: Freq=%llu MHz, Min=%.1f, Avg=%.1f, Max=%.1f dB\n", 
                   step_count, freq / 1000000, min_power, avg_power, max_power);
            
            // 전체 스펙트럼 범위 계산
            uint64_t total_range = wideband_state.end_freq - wideband_state.start_freq;
            uint64_t extended_range = total_range + SAMPLE_RATE;  // 양쪽 확장
            size_t total_bins = wideband_state.full_spectrum.size();
            
            // 배열 시작 주파수 = start_freq - SAMPLE_RATE/2
            uint64_t array_start_freq = wideband_state.start_freq - SAMPLE_RATE/2;
            
            // 현재 주파수의 시작 위치 계산
            int64_t freq_offset = (int64_t)freq - (int64_t)array_start_freq;
            size_t base_index = (size_t)((double)freq_offset / (double)extended_range * (double)total_bins);
            
            // FFT 결과의 각 빈을 전체 스펙트럼에 매핑
            double hz_per_bin = (double)SAMPLE_RATE / (double)FFT_SIZE;
            double bins_per_mhz = (double)total_bins / (double)(extended_range / 1000000);
            
            // 🔴 디버그: 매핑 정보 출력
            printf("  -> base_index=%zu, total_bins=%zu, bins_per_mhz=%.2f\n",
                   base_index, total_bins, bins_per_mhz);
            printf("  -> FFT covers: %.1f ~ %.1f MHz\n",
                   (freq - SAMPLE_RATE/2) / 1e6, (freq + SAMPLE_RATE/2) / 1e6);
            printf("  -> Array covers: %.1f ~ %.1f MHz\n",
                   array_start_freq / 1e6, (array_start_freq + extended_range) / 1e6);
            
            size_t min_written_index = total_bins;
            size_t max_written_index = 0;
            size_t num_written = 0;
            
            {
                std::lock_guard<std::mutex> lock(wideband_state.mutex);
                
                // 사용할 FFT 범위: 중심에서 ±STEP_SIZE/2 만 사용
                uint64_t use_range = (STEP_SIZE_MHZ * 1000000ULL) / 2;
                
                for (size_t i = 0; i < avg_spectrum.size(); i++) {
                    // FFT 빈 i가 나타내는 주파수 오프셋 (중심 주파수 기준)
                    double freq_offset_hz = (i - FFT_SIZE / 2.0) * hz_per_bin;
                    
                    // 중심 주파수로부터 너무 멀면 건너뛰기
                    if (fabs(freq_offset_hz) > use_range) continue;
                    
                    double freq_offset_mhz = freq_offset_hz / 1000000.0;
                    int64_t global_index = base_index + (int64_t)(freq_offset_mhz * bins_per_mhz);
                    
                    if (global_index >= 0 && global_index < (int64_t)total_bins) {
                        num_written++;
                        if ((size_t)global_index < min_written_index) min_written_index = global_index;
                        if ((size_t)global_index > max_written_index) max_written_index = global_index;
                        
                        float new_value = avg_spectrum[i];
                        
                        // 직접 덮어쓰기 (블렌딩 없음)
                        wideband_state.avg_spectrum_acc[global_index] = new_value;
                        wideband_state.full_spectrum[global_index] = new_value;
                        
                        if (wideband_state.peak_hold_enabled) {
                            if (new_value > wideband_state.peak_spectrum[global_index]) {
                                wideband_state.peak_spectrum[global_index] = new_value;
                            } else {
                                wideband_state.peak_spectrum[global_index] -= 0.05f;
                            }
                        }
                    }
                }
            }
            
            printf("  -> Written %zu bins: index %zu ~ %zu (%.1f ~ %.1f MHz)\n",
                   num_written, min_written_index, max_written_index,
                   (wideband_state.start_freq - SAMPLE_RATE/2)/1e6 + min_written_index/bins_per_mhz,
                   (wideband_state.start_freq - SAMPLE_RATE/2)/1e6 + max_written_index/bins_per_mhz);
            
            // 다음 주파수로
            freq += STEP_SIZE_MHZ * 1000000ULL;
        }
        
        // 워터폴에 추가
        {
            std::lock_guard<std::mutex> lock(wideband_state.mutex);
            wideband_state.add_waterfall_line();
        }
        printf("=== SWEEP #%d END ===\n", wideband_state.sweep_count);
        printf("  다음 스윕에서는 현재 주파수 범위(%llu~%llu MHz)만 표시됩니다\n\n",
               wideband_state.start_freq / 1000000,
               wideband_state.end_freq / 1000000);
    }
    
    // 정리
    bladerf_enable_module(dev, CHANNEL, false);
    bladerf_close(dev);
    
    printf("\n✓ BladeRF 스윕 스레드 종료\n");
}

// ==================== OpenGL 렌더링 ====================
void render_spectrum() {
    glClear(GL_COLOR_BUFFER_BIT);
    
    std::lock_guard<std::mutex> lock(wideband_state.mutex);
    
    size_t total_bins = wideband_state.full_spectrum.size();
    if (total_bins == 0) return;
    
    float db_min = wideband_state.db_min;
    float db_max = wideband_state.db_max;
    
    // 배열은 확장되어 있지만 표시는 start_freq ~ end_freq만
    uint64_t display_range = wideband_state.end_freq - wideband_state.start_freq;
    uint64_t array_range = display_range + SAMPLE_RATE;
    
    // 배열에서 실제 표시할 범위의 시작/끝 인덱스 계산
    size_t display_start_index = (size_t)(SAMPLE_RATE / 2.0 / array_range * total_bins);
    size_t display_end_index = display_start_index + 
                               (size_t)((double)display_range / array_range * total_bins);
    size_t num_points = display_end_index - display_start_index;
    
    // ========== 상단: 파워 스펙트럼 (0.0 ~ 1.0) ==========
    
    // 배경 그리드 (상단)
    glColor3f(0.15f, 0.15f, 0.15f);
    glBegin(GL_LINES);
    
    // 수평선 (dB 레벨) - 10개
    for (int i = 0; i <= 10; i++) {
        float y = 0.05f + 0.9f * i / 10.0f;
        glVertex2f(-0.95f, y);
        glVertex2f(0.95f, y);
    }
    
    // 수직선 (주파수) - 10개
    for (int i = 0; i <= 10; i++) {
        float x = -0.95f + 1.9f * i / 10.0f;
        glVertex2f(x, 0.05f);
        glVertex2f(x, 0.95f);
    }
    
    glEnd();
    
    // dB 레벨 라벨 (왼쪽)
    glColor3f(0.7f, 0.7f, 0.7f);
    for (int i = 0; i <= 10; i++) {
        float y = 0.05f + 0.9f * i / 10.0f;
        float db_value = db_min + (db_max - db_min) * i / 10.0f;
        char label[32];
        snprintf(label, sizeof(label), "%.0f", db_value);
        draw_text_gl(-0.99f, y - 0.01f, label);
    }
    
    // 주파수 라벨 (하단)
    uint64_t freq_range = wideband_state.end_freq - wideband_state.start_freq;
    for (int i = 0; i <= 10; i++) {
        float x = -0.95f + 1.9f * i / 10.0f;
        uint64_t freq_mhz = wideband_state.start_freq / 1000000 + 
                           (freq_range / 1000000) * i / 10;
        char label[32];
        snprintf(label, sizeof(label), "%llu", freq_mhz);
        draw_text_gl(x - 0.03f, 0.01f, label);
    }
    
    // 파워 스펙트럼 그리기
    glColor3f(0.0f, 1.0f, 0.0f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_STRIP);
    
    for (size_t i = 0; i < num_points; i++) {
        float x = -0.95f + 1.9f * i / num_points;
        float db = wideband_state.full_spectrum[display_start_index + i];
        
        // dB를 0.05 ~ 0.95로 매핑 (화면 상단)
        float y = 0.05f + 0.9f * (db - db_min) / (db_max - db_min);
        y = fmaxf(0.05f, fminf(0.95f, y));
        
        glVertex2f(x, y);
    }
    
    glEnd();
    
    // Peak hold 그리기 (반투명 노란색)
    if (wideband_state.peak_hold_enabled) {
        glColor4f(1.0f, 1.0f, 0.0f, 0.6f);  // 노란색, 60% 투명도
        glBegin(GL_LINE_STRIP);
        
        for (size_t i = 0; i < num_points; i++) {
            float x = -0.95f + 1.9f * i / num_points;
            float db = wideband_state.peak_spectrum[display_start_index + i];
            
            float y = 0.05f + 0.9f * (db - db_min) / (db_max - db_min);
            y = fmaxf(0.05f, fminf(0.95f, y));
            
            glVertex2f(x, y);
        }
        
        glEnd();
    }
    
    glLineWidth(1.0f);
    
    // 중심선 (구분선)
    glColor3f(0.8f, 0.8f, 0.8f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(-1.0f, 0.0f);
    glVertex2f(1.0f, 0.0f);
    glEnd();
    glLineWidth(1.0f);
    
    // ========== 하단: 워터폴 (-0.95 ~ -0.05) ==========
    
    // 배경 그리드 (하단)
    glColor3f(0.15f, 0.15f, 0.15f);
    glBegin(GL_LINES);
    
    // 수평선
    for (int i = 0; i <= 10; i++) {
        float y = -0.95f + 0.9f * i / 10.0f;
        glVertex2f(-0.95f, y);
        glVertex2f(0.95f, y);
    }
    
    // 수직선
    for (int i = 0; i <= 10; i++) {
        float x = -0.95f + 1.9f * i / 10.0f;
        glVertex2f(x, -0.95f);
        glVertex2f(x, -0.05f);
    }
    
    glEnd();
    
    // 주파수 라벨 (상단)
    glColor3f(0.7f, 0.7f, 0.7f);
    for (int i = 0; i <= 10; i++) {
        float x = -0.95f + 1.9f * i / 10.0f;
        uint64_t freq_mhz = wideband_state.start_freq / 1000000 + 
                           (freq_range / 1000000) * i / 10;
        char label[32];
        snprintf(label, sizeof(label), "%llu", freq_mhz);
        draw_text_gl(x - 0.03f, -0.03f, label);
    }
    
    // 워터폴 그리기 - 픽셀 기반
    size_t history_size = wideband_state.waterfall_history.size();
    if (history_size > 0) {
        // 최신 데이터(index=history_size-1)가 위쪽, 오래된 데이터(index=0)가 아래쪽
        for (size_t line = 0; line < history_size; line++) {
            // 역순으로 접근: 가장 최신 데이터부터
            size_t data_index = history_size - 1 - line;
            const std::vector<float>& spectrum_line = wideband_state.waterfall_history[data_index];
            if (spectrum_line.size() < display_end_index) continue;
            
            // y 좌표: line=0(최신)이 위쪽(-0.05), line=max(오래됨)가 아래쪽(-0.95)
            float y_base = -0.05f - 0.9f * line / WATERFALL_HISTORY;
            float y_next = -0.05f - 0.9f * (line + 1) / WATERFALL_HISTORY;
            
            glBegin(GL_QUADS);
            
            // 표시 범위만 그리기
            for (size_t i = 0; i < num_points - 1; i++) {
                size_t array_index = display_start_index + i;
                float x1 = -0.95f + 1.9f * i / num_points;
                float x2 = -0.95f + 1.9f * (i + 1) / num_points;
                
                float db = spectrum_line[array_index];
                float r, g, b;
                value_to_color(db, db_min, db_max, r, g, b);
                
                glColor3f(r, g, b);
                glVertex2f(x1, y_base);
                glVertex2f(x2, y_base);
                glVertex2f(x2, y_next);
                glVertex2f(x1, y_next);
            }
            
            glEnd();
        }
    }
    
    // 정보 표시 (윈도우 타이틀)
    char title[256];
    if (wideband_state.adjust_mode) {
        snprintf(title, sizeof(title), 
                 "BladeRF Spectrum | Sweep #%d | [ADJUST MODE] dB: %.0f ~ %.0f | ↑↓: Max | ←→: Min | F: Exit | R: Reset", 
                 wideband_state.sweep_count, db_min, db_max);
    } else {
        snprintf(title, sizeof(title), 
                 "BladeRF Spectrum | Sweep #%d | %llu MHz | dB: %.0f ~ %.0f | F: Adjust Mode | R: Reset | ESC: Quit", 
                 wideband_state.sweep_count, wideband_state.current_freq / 1000000, db_min, db_max);
    }
    glfwSetWindowTitle(window, title);
}

// ==================== 키보드 입력 처리 ====================
void process_input() {
    static bool f_pressed = false;
    static bool up_pressed = false;
    static bool down_pressed = false;
    static bool left_pressed = false;
    static bool right_pressed = false;
    static bool r_pressed = false;
    
    // F 키 - 조정 모드 토글
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
        if (!f_pressed) {
            wideband_state.adjust_mode = !wideband_state.adjust_mode;
            f_pressed = true;
        }
    } else {
        f_pressed = false;
    }
    
    // 조정 모드일 때 화살표 키
    if (wideband_state.adjust_mode) {
        // ↑↓: db_max 조정
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            if (!up_pressed) {
                wideband_state.db_max += 5.0f;
                if (wideband_state.db_max > 20.0f) wideband_state.db_max = 20.0f;
                up_pressed = true;
            }
        } else {
            up_pressed = false;
        }
        
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            if (!down_pressed) {
                wideband_state.db_max -= 5.0f;
                if (wideband_state.db_max < wideband_state.db_min + 10.0f) {
                    wideband_state.db_max = wideband_state.db_min + 10.0f;
                }
                down_pressed = true;
            }
        } else {
            down_pressed = false;
        }
        
        // ←→: db_min 조정
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            if (!left_pressed) {
                wideband_state.db_min -= 5.0f;
                if (wideband_state.db_min < -120.0f) wideband_state.db_min = -120.0f;
                left_pressed = true;
            }
        } else {
            left_pressed = false;
        }
        
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            if (!right_pressed) {
                wideband_state.db_min += 5.0f;
                if (wideband_state.db_min > wideband_state.db_max - 10.0f) {
                    wideband_state.db_min = wideband_state.db_max - 10.0f;
                }
                right_pressed = true;
            }
        } else {
            right_pressed = false;
        }
    }
    
    // R 키 - 리셋
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
        if (!r_pressed) {
            wideband_state.db_min = -80.0f;
            wideband_state.db_max = -10.0f;
            r_pressed = true;
        }
    } else {
        r_pressed = false;
    }
    
    // ESC 키 - 종료
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        wideband_state.running = false;
    }
}

// ==================== 메인 함수 ====================
int main(int argc, char** argv) {
    printf("\n");
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║   BladeRF 광대역 스펙트럼 분석기 v2.0   ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    printf("\n");
    
    // GLUT 초기화 (텍스트 렌더링용)
    glutInit(&argc, argv);
    
    // GLFW 초기화
    if (!glfwInit()) {
        fprintf(stderr, "❌ GLFW 초기화 실패\n");
        return 1;
    }
    
    // 윈도우 생성
    window = glfwCreateWindow(window_width, window_height, 
                              "BladeRF Spectrum Analyzer", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "❌ 윈도우 생성 실패\n");
        glfwTerminate();
        return 1;
    }
    
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync
    
    // OpenGL 설정
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // 안티앨리어싱
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    printf("✓ OpenGL 윈도우 초기화 완료\n");
    
    // BladeRF 스윕 스레드 시작
    std::thread sweep_thread(bladerf_sweep_thread);
    
    // 메인 렌더링 루프
    while (!glfwWindowShouldClose(window) && wideband_state.running) {
        process_input();
        render_spectrum();
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // 정리
    printf("\n\n종료 중...\n");
    wideband_state.running = false;
    sweep_thread.join();
    
    glfwDestroyWindow(window);
    glfwTerminate();
    
    printf("\n");
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║          프로그램 정상 종료               ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    printf("\n");
    
    return 0;
}