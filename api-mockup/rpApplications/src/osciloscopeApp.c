/**
* $Id: $
*
* @brief Red Pitaya application library osciloscope module interface
*
* @Author Red Pitaya
*
* (c) Red Pitaya  http://www.redpitaya.com
*
* This part of code is written in C programming language.
* Please visit http://en.wikipedia.org/wiki/C_(programming_language)
* for more details on the language used herein.
*/


#include <math.h>
#include <float.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include "osciloscopeApp.h"
#include "common.h"
#include "../../rpbase/src/common.h"

bool auto_freRun_mode = false;
bool acqRunning = false;
bool clear = false;
bool continuousMode = false;
uint32_t viewSize = VIEW_SIZE_DEFAULT;
float *view;
float ch1_ampOffset, ch2_ampOffset, math_ampOffset;
float ch1_ampScale,  ch2_ampScale,  math_ampScale;
float ch1_probeAtt, ch2_probeAtt;
bool ch1_inverted = false; bool ch2_inverted = false, math_inverted = false;
float timeScale=1, timeOffset=0;
rpApp_osc_trig_sweep_t trigSweep;
rpApp_osc_trig_source_t trigSource = RPAPP_OSC_TRIG_SRC_CH1;
rpApp_osc_trig_slope_t trigSlope = RPAPP_OSC_TRIG_SLOPE_PE;
rpApp_osc_math_oper_t operation;
rp_channel_t mathSource1, mathSource2;

float samplesPerDivision = (float) VIEW_SIZE_DEFAULT / (float) DIVISIONS_COUNT_X;

pthread_t mainThread = (pthread_t) -1;
pthread_mutex_t mutex;


int osc_Init() {
    pthread_mutex_init(&mutex, NULL);
    view = calloc(3 * viewSize, sizeof(float));
    if (view == NULL) {
        free(view);
        view = NULL;
        return RP_EAA;
    }
    return RP_OK;
}

int osc_Release() {
    STOP_THREAD(mainThread);
    pthread_mutex_destroy(&mutex);
    if (view != NULL) {
        free(view);
        view = NULL;
    }
    return RP_OK;
}

int osc_SetDefaultValues() {
    ECHECK_APP(osc_setAmplitudeOffset(RPAPP_OSC_SOUR_CH1, 0));
    ECHECK_APP(osc_setAmplitudeOffset(RPAPP_OSC_SOUR_CH2, 0));
    ECHECK_APP(osc_setAmplitudeOffset(RPAPP_OSC_SOUR_MATH, 0));
    ECHECK_APP(osc_setAmplitudeScale(RPAPP_OSC_SOUR_CH1, 1));
    ECHECK_APP(osc_setAmplitudeScale(RPAPP_OSC_SOUR_CH2, 1));
    ECHECK_APP(osc_setAmplitudeScale(RPAPP_OSC_SOUR_MATH, 1));
    ECHECK_APP(osc_setProbeAtt(RP_CH_1, 1));
    ECHECK_APP(osc_setProbeAtt(RP_CH_2, 1));
    ECHECK_APP(osc_setInputGain(RP_CH_1, RPAPP_OSC_IN_GAIN_LV))
    ECHECK_APP(osc_setInputGain(RP_CH_2, RPAPP_OSC_IN_GAIN_LV))
    ECHECK_APP(osc_setTimeOffset(0));
    ECHECK_APP(osc_setTriggerSlope(RPAPP_OSC_TRIG_SLOPE_PE));
    ECHECK_APP(rp_AcqSetTriggerSrc(RP_TRIG_SRC_CHA_PE));
    ECHECK_APP(osc_setTriggerLevel(0));
    ECHECK_APP(osc_setTriggerSweep(RPAPP_OSC_TRIG_AUTO));
    ECHECK_APP(osc_setTriggerSource(RPAPP_OSC_TRIG_SRC_CH1));
    ECHECK_APP(osc_setTimeScale(1));
    ECHECK_APP(osc_setMathOperation(RPAPP_OSC_MATH_NONE));
    ECHECK_APP(osc_setMathSources(RP_CH_1, RP_CH_2));

    return RP_OK;
}

int osc_run() {
    clearView();
    ECHECK_APP(threadSafe_acqStart());
    ECHECK_APP(osc_setTriggerSource(trigSource));
    START_THREAD(mainThread, mainThreadFun);
    return RP_OK;
}

int osc_stop() {
    ECHECK_APP(threadSafe_acqStop());
    return RP_OK;
}

int osc_reset() {
    clearView();
    STOP_THREAD(mainThread);
    ECHECK_APP(threadSafe_acqStop());
    ECHECK_APP(osc_SetDefaultValues());
    return RP_OK;
}

int osc_single() {
    if (trigSweep != RPAPP_OSC_TRIG_SINGLE) {
        ECHECK_APP(osc_setTriggerSweep(RPAPP_OSC_TRIG_SINGLE));
    }
    ECHECK_APP(threadSafe_acqStart());
    ECHECK_APP(waitToFillPreTriggerBuffer(0));
    ECHECK_APP(osc_setTriggerSource(trigSource));
    return RP_OK;
}

int osc_autoScale() {
    float period, vpp, vMean;
    bool isAutoScaled = false;

    for (rpApp_osc_source source = RPAPP_OSC_SOUR_CH1; source <= RPAPP_OSC_SOUR_CH2; ++source) {
        ECHECK_APP(osc_measureVpp(source, &vpp));
        ECHECK_APP(osc_measureMeanVoltage(source, &vMean));

        // If there is signal on input
        if (fabs(vpp) > SIGNAL_EXISTENCE) {
            if (!isAutoScaled) {
                // set time scale only based on one channel
                ECHECK_APP(osc_measurePeriod(source, &period));
                ECHECK_APP(osc_setTimeOffset(AUTO_SCALE_TIME_OFFSET));
                ECHECK_APP(osc_setTimeScale(period * AUTO_SCALE_PERIOD_COUNT / DIVISIONS_COUNT_X));
                isAutoScaled = true;
            }
            ECHECK_APP(osc_setAmplitudeOffset(source, -vMean));
            // Calculate scale
            float scale = (float) (vpp * AUTO_SCALE_AMP_SCA_FACTOR / DIVISIONS_COUNT_Y * (source == RPAPP_OSC_SOUR_CH1 ? ch1_probeAtt : ch2_probeAtt));
            ECHECK_APP(osc_setAmplitudeScale(source, roundUpTo125(scale)));
        }
    }
    if (isAutoScaled) {
        if (trigSweep != RPAPP_OSC_TRIG_AUTO) {
            osc_setTriggerSweep(RPAPP_OSC_TRIG_AUTO);
        }
        return RP_OK;
    }
    else {
        if (trigSweep != RPAPP_OSC_TRIG_NORMAL) {
            osc_setTriggerSweep(RPAPP_OSC_TRIG_NORMAL);
        }
        return RP_APP_ENS;
    }
}

int osc_isRunning(bool *running) {
    *running = acqRunning;
    return RP_OK;
}

int osc_setTimeScale(float scale) {
    float maxDeltaSample = 125000000.0f * scale / 1000.0f / samplesPerDivision;
    float ratio = (float) ADC_BUFFER_SIZE / (float) viewSize;

    if (maxDeltaSample / 65536.0f > ratio) {
        return RP_EOOR;
    }

    rp_acq_decimation_t decimation;

    // contition: viewBuffer cannot be larger than adcBuffer
    if (maxDeltaSample <= ratio) {
        decimation = RP_DEC_1;
    }
    else if (maxDeltaSample / 8.0f <= ratio) {
        decimation = RP_DEC_8;
    }
    else if (maxDeltaSample / 64.0f <= ratio) {
        decimation = RP_DEC_64;
    }
    else if (maxDeltaSample / 1024.0f <= ratio) {
        decimation = RP_DEC_1024;
    }
    else if (maxDeltaSample / 8192.0f <= ratio) {
        decimation = RP_DEC_8192;
    }
    else {
        decimation = RP_DEC_65536;
    }

    pthread_mutex_lock(&mutex);
    if (scale < CONTIOUS_MODE_SCALE_THRESHOLD) {
        ECHECK_APP_MUTEX(mutex, rp_AcqSetArmKeep(false))
        continuousMode = false;
    } else {
        ECHECK_APP_MUTEX(mutex, rp_AcqSetArmKeep(true))
        continuousMode = true;
    }
    clearView();
    timeScale = scale;
    ECHECK_APP_MUTEX(mutex, rp_AcqSetDecimation(decimation))
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

int osc_getTimeScale(float *division) {
    *division = timeScale;
    return RP_OK;
}

int osc_setTimeOffset(float offset) {
    float deltaSample = timeToIndex(timeScale) / samplesPerDivision;
    if (offset < ((int)viewSize/2-ADC_BUFFER_SIZE/2) * deltaSample || offset > indexToTime((int64_t) MAX_UINT)) {
        return RP_EOOR;
    }

    pthread_mutex_lock(&mutex);
    clearView();
    timeOffset = offset;
    ECHECK_APP_MUTEX(mutex, rp_AcqSetTriggerDelayNs((int64_t)(offset * MILLI_TO_NANO)));
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

int osc_getTimeOffset(float *offset) {
    *offset = timeOffset;
    return RP_OK;
}

int osc_setProbeAtt(rp_channel_t channel, float att) {
    CHANNEL_ACTION(channel,
                   ch1_probeAtt = att,
                   ch2_probeAtt = att)
    EXECUTE_ATOMICALLY(mutex, clearView());
    return RP_OK;
}

int osc_getProbeAtt(rp_channel_t channel, float *att) {
    CHANNEL_ACTION(channel,
                   *att = ch1_probeAtt,
                   *att = ch2_probeAtt)
    return 0;
}

int osc_setInputGain(rp_channel_t channel, rpApp_osc_in_gain_t gain) {
    pthread_mutex_lock(&mutex);
    clearView();
    switch (gain) {
        case RPAPP_OSC_IN_GAIN_LV:
            ECHECK_APP_MUTEX(mutex, rp_AcqSetGain(channel, RP_LOW));
            break;
        case RPAPP_OSC_IN_GAIN_HV:
            ECHECK_APP_MUTEX(mutex, rp_AcqSetGain(channel, RP_HIGH));
            break;
        default:
            return RP_EOOR;
    }
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

int osc_getInputGain(rp_channel_t channel, rpApp_osc_in_gain_t *gain) {
    rp_pinState_t state;
    ECHECK_APP(rp_AcqGetGain(channel, &state));
    switch (state) {
        case RP_LOW:
            *gain = RPAPP_OSC_IN_GAIN_LV;
            break;
        case RP_HIGH:
            *gain = RPAPP_OSC_IN_GAIN_HV;
            break;
        default:
            return RP_EOOR;
    }
    return RP_OK;
}

int osc_setAmplitudeScale(rpApp_osc_source source, float scale) {
    float offset, currScale;
    pthread_mutex_lock(&mutex);
    ECHECK_APP_MUTEX(mutex, osc_getAmplitudeOffset(source, &offset));
    ECHECK_APP_MUTEX(mutex, osc_getAmplitudeScale(source, &currScale));
    offset = offset / currScale;
    SOURCE_ACTION(source,
                  ch1_ampScale = scale,
                  ch2_ampScale = scale,
                  math_ampScale = scale)
    clearView();
    offset *= scale;
    pthread_mutex_unlock(&mutex);
    if (!isnan(offset)) {
        ECHECK_APP(osc_setAmplitudeOffset(source, offset));
    }
    return RP_OK;
}

int osc_getAmplitudeScale(rpApp_osc_source source, float *scale) {
    SOURCE_ACTION(source,
                  *scale = ch1_ampScale,
                  *scale = ch2_ampScale,
                  *scale = math_ampScale)
    return RP_OK;
}

int osc_setAmplitudeOffset(rpApp_osc_source source, float offset) {
    pthread_mutex_lock(&mutex);
    SOURCE_ACTION(source,
                  ch1_ampOffset = offset,
                  ch2_ampOffset = offset,
                  math_ampOffset = offset)
    clearView();
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

int osc_getAmplitudeOffset(rpApp_osc_source source, float *offset) {
    SOURCE_ACTION(source,
                  *offset = ch1_ampOffset,
                  *offset = ch2_ampOffset,
                  *offset = math_ampOffset)
    return RP_OK;
}

int osc_setTriggerSource(rpApp_osc_trig_source_t triggerSource) {
    pthread_mutex_lock(&mutex);
    if (trigSource != triggerSource) {
        clearView();
    }
    rp_acq_trig_src_t src;
    switch (triggerSource) {
        case RPAPP_OSC_TRIG_SRC_CH1:
            if (trigSlope == RPAPP_OSC_TRIG_SLOPE_NE) {
                src = RP_TRIG_SRC_CHA_NE;
            }
            else {
                src = RP_TRIG_SRC_CHA_PE;
            }
            break;
        case RPAPP_OSC_TRIG_SRC_CH2:
            if (trigSlope == RPAPP_OSC_TRIG_SLOPE_NE) {
                src = RP_TRIG_SRC_CHB_NE;
            }
            else {
                src = RP_TRIG_SRC_CHB_PE;
            }
            break;
        case RPAPP_OSC_TRIG_SRC_EXTERNAL:
            if (trigSlope == RPAPP_OSC_TRIG_SLOPE_NE) {
                src = RP_TRIG_SRC_EXT_NE;
            }
            else {
                src = RP_TRIG_SRC_EXT_PE;
            }
            break;
        default:
            return RP_EOOR;
    }

    trigSource = triggerSource;
    ECHECK_APP_MUTEX(mutex, rp_AcqSetTriggerSrc(src));
    pthread_mutex_unlock(&mutex);

    return RP_OK;
}

int osc_getTriggerSource(rpApp_osc_trig_source_t *triggerSource) {
    *triggerSource = trigSource;
    return RP_OK;
}

int osc_setTriggerSlope(rpApp_osc_trig_slope_t slope) {
    pthread_mutex_lock(&mutex);
    clearView();
    rp_acq_trig_src_t src;
    switch (trigSource) {
        case RPAPP_OSC_TRIG_SRC_CH1:
            if (slope == RPAPP_OSC_TRIG_SLOPE_NE) {
                src = RP_TRIG_SRC_CHA_NE;
            }
            else {
                src = RP_TRIG_SRC_CHA_PE;
            }
            break;
        case RPAPP_OSC_TRIG_SRC_CH2:
            if (slope == RPAPP_OSC_TRIG_SLOPE_NE) {
                src = RP_TRIG_SRC_CHB_NE;
            }
            else {
                src = RP_TRIG_SRC_CHB_PE;
            }
            break;
        case RPAPP_OSC_TRIG_SRC_EXTERNAL:
            if (slope == RPAPP_OSC_TRIG_SLOPE_NE) {
                src = RP_TRIG_SRC_EXT_NE;
            }
            else {
                src = RP_TRIG_SRC_EXT_PE;
            }
            break;
        default:
            return RP_EOOR;
    }

    trigSlope = slope;
    ECHECK_APP_MUTEX(mutex, rp_AcqSetTriggerSrc(src));
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

int osc_getTriggerSlope(rpApp_osc_trig_slope_t *slope) {
    *slope = trigSlope;
    return RP_OK;
}

int osc_setTriggerLevel(float level) {
    pthread_mutex_lock(&mutex);
    clearView();
    ECHECK_APP_MUTEX(mutex, rp_AcqSetTriggerLevel(level));
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

int osc_getTriggerLevel(float *level) {
    return rp_AcqGetTriggerLevel(level);
}

int osc_setTriggerSweep(rpApp_osc_trig_sweep_t sweep) {
    EXECUTE_ATOMICALLY(mutex, clearView());
    switch (sweep) {
        case RPAPP_OSC_TRIG_SINGLE:
            break;
        case RPAPP_OSC_TRIG_AUTO:
        EXECUTE_ATOMICALLY(mutex, auto_freRun_mode = false)
        case RPAPP_OSC_TRIG_NORMAL:
            if (!acqRunning) {
                ECHECK_APP(threadSafe_acqStart());
            }
            break;
        default:
            return RP_EOOR;
    }
    trigSweep = sweep;
    return RP_OK;
}

int osc_getTriggerSweep(rpApp_osc_trig_sweep_t *sweep) {
    *sweep = trigSweep;
    return RP_OK;
}

int osc_setInverted(rpApp_osc_source source, bool inverted) {
    pthread_mutex_lock(&mutex);
    SOURCE_ACTION(source,
                  ch1_inverted = inverted,
                  ch2_inverted = inverted,
                  math_inverted = inverted)
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

int osc_isInverted(rpApp_osc_source source, bool *inverted) {
    SOURCE_ACTION(source,
                  *inverted = ch1_inverted,
                  *inverted = ch2_inverted,
                  *inverted = math_inverted)
    return RP_OK;
}

int osc_getViewPart(float *ratio) {
    *ratio = ((float)viewSize * (float)timeToIndex(timeScale) / samplesPerDivision) / (float)ADC_BUFFER_SIZE;
    return RP_OK;
}

int osc_measureVpp(rpApp_osc_source source, float *Vpp) {
    float resMax, resMin, max = -FLT_MAX, min = FLT_MAX;
    for (int i = 0; i < viewSize; ++i) {
        if (view[source*viewSize + i] > max) {
            max = view[source*viewSize + i];
        }
        if (view[source*viewSize + i] < min) {
            min = view[source*viewSize + i];
        }
    }
    ECHECK_APP(unscaleAmplitudeChannel(source, max, &resMax));
    ECHECK_APP(unscaleAmplitudeChannel(source, min, &resMin));
    *Vpp = resMax - resMin;
    return RP_OK;
}

int osc_measureMeanVoltage(rpApp_osc_source source, float *meanVoltage) {
    float sum = 0;
    for (int i = 0; i < viewSize; ++i) {
        sum += view[source*viewSize + i];
    }
    ECHECK_APP(unscaleAmplitudeChannel(source, sum / viewSize, meanVoltage));
    return RP_OK;
}

int osc_measureMaxVoltage(rpApp_osc_source source, float *Vmax) {
    float max = -FLT_MAX;
    for (int i = 0; i < viewSize; ++i) {
        if (view[source*viewSize + i] > max) {
            max = view[source*viewSize + i];
        }
    }
    ECHECK_APP(unscaleAmplitudeChannel(source, max, Vmax));
    return RP_OK;
}

int osc_measureMinVoltage(rpApp_osc_source source, float *Vmin) {
    float min = FLT_MAX;
    for (int i = 0; i < viewSize; ++i) {
        if (view[source*viewSize + i] < min) {
            min = view[source*viewSize + i];
        }
    }
    ECHECK_APP(unscaleAmplitudeChannel(source, min, Vmin));
    return RP_OK;
}

int osc_measureFrequency(rpApp_osc_source source, float *frequency) {
    float period;
    ECHECK_APP(osc_measurePeriod(source, &period));
    *frequency = (float) (1 / (period / 1000.0));
    return RP_OK;
}

int osc_measurePeriod(rpApp_osc_source source, float *period) {
    uint32_t dataSize = source == RPAPP_OSC_SOUR_MATH ? viewSize : VIEW_SIZE_DEFAULT;
    float data[dataSize];
    pthread_mutex_lock(&mutex);
    if (source == RPAPP_OSC_SOUR_MATH) {
        for (int i = 0; i < dataSize; ++i) {
            data[i] = view[RPAPP_OSC_SOUR_MATH*viewSize + i];
        }
    } else {
        ECHECK_APP_MUTEX(mutex, rp_AcqGetLatestDataV((rp_channel_t)source, &dataSize, data));
    }
    pthread_mutex_unlock(&mutex);

    float mean = 0;
    for (int i = 0; i < dataSize; ++i) {
        mean += data[i];
    }

    mean = mean / dataSize;
    for (int i = 0; i < dataSize; ++i){
        data[i] -= mean;
    }

    // calculate signal correlation
    float xcorr[dataSize];
    for (int i = 0; i < dataSize; ++i) {
        xcorr[i] = 0;
        for (int j = 0; j < dataSize-i; ++j) {
            xcorr[i] += data[j] * data[j+i];
        }
        xcorr[i] /= dataSize-i;
    }

    // The main problem is the presence lot of noise in the signal
    // We can filter correlation function and differentiate it to find local maximum, but it could fail on high frequencies I suppose
    // So lets try to find local maximum logically, idea is:
    // signal: ZxxbbbbbaaAaaBbbbbxxYxbbbbBaaAaaa
    // 'a' - values below acceptable threshold
    // 'b', 'x', 'y' - values above acceptable threshold
    // 'Y' - local maximum value
    // 'Z' - reference value
    // 'x' - almost y
    // need find left 'A', then we can find left 'B'
    // then can need find right 'A', then can find right 'B'
    // then we can find 'Y' between left and right 'B'
    // then we can find left and right 'x'
    // guess extreme point locates in the middle of left and right 'x'
    // we can not use 'Y' only because it could be (x + noise)

    int left_idx = 0;
    int right_idx = 0;
    int left_edge_idx = 0;
    int right_edge_idx = dataSize-2;

    // search for left point where correlation function is less than it's expected
    for (int i = 1; i < dataSize-1; ++i) {
        if((xcorr[i] / xcorr[0]) < PERIOD_EXISTS_MIN_THRESHOLD) {
            left_edge_idx = i;
            break;
        }
    }

    if(left_edge_idx == 0) {
#if 0
        fprintf(stderr, "left_edge_idx == 0\n");
        fprintf(stderr, "source = %d\n", source);
        fprintf(stderr, "dataSize = %d\n", dataSize);
#endif
        return RP_APP_ECP;
    }

    // search for left point where correlation function is greater than it's expected
    for (int i = left_edge_idx; i < dataSize-1; ++i) {
        if((xcorr[i] / xcorr[0]) >= PERIOD_EXISTS_MAX_THRESHOLD) {
            left_idx = i;
            break;
        }
    }

    if(left_idx == 0) {
#if 0		
        fprintf(stderr, "left_idx == 0\n");
        fprintf(stderr, "source = %d\n", source);
        fprintf(stderr, "dataSize = %d\n", dataSize);
#endif
        return RP_APP_ECP;
    }

    // search for right point where correlation function is less than it's expected
    for (int i = left_idx; i < dataSize-1; ++i) {
        if((xcorr[i] / xcorr[0]) < PERIOD_EXISTS_MIN_THRESHOLD) {
            right_edge_idx = i;
            break;
        }
    }
    
    // search for right point where correlation function is greater than it's expected
    for (int i = right_edge_idx; i >= left_idx; --i) {
        if((xcorr[i] / xcorr[0]) >= PERIOD_EXISTS_MAX_THRESHOLD) {
            right_idx = i;
            break;
        }
    }

    // search for local maximum
    float loc_max = xcorr[left_idx];
    int max_idx = left_idx;
    for (int i = left_idx; i <= right_idx; ++i) {
        if(loc_max < xcorr[i]) {
            loc_max = xcorr[i];
            max_idx = i;
        }
    }

    // search for left point which is almost equal to maximum
    int left_amax_idx = max_idx;
    int right_amax_idx = max_idx;
    for (int i = left_idx; i <= right_idx; ++i) {
        if(xcorr[i] >= loc_max * PERIOD_EXISTS_PEAK_THRESHOLD) {
            left_amax_idx = i;
            break;
        }
    }

    // search for right point which is almost equal to maximum
    for (int i = right_edge_idx; i >= left_idx; --i) {
        if(xcorr[i] >= loc_max * PERIOD_EXISTS_PEAK_THRESHOLD) {
            right_amax_idx = i;
            break;
        }
    }

    // guess extreme point locates between 'left_amax_idx' and 'right_amax_idx'
    *period = indexToTime((left_amax_idx + right_amax_idx) / 2);

#if 0
    static int counter = 0;
    if((*period >= 1.1f) || ((counter % 20) == 0)) {
        fprintf(stderr, "source = %d\n", source);
        fprintf(stderr, "dataSize = %d\n", dataSize);

        fprintf(stderr, "left_edge_idx = %d\n", left_edge_idx);
        fprintf(stderr, "left_idx = %d\n", left_idx);

        fprintf(stderr, "right_edge_idx = %d\n", right_edge_idx);
        fprintf(stderr, "right_idx = %d\n", right_idx);

        fprintf(stderr, "max_idx = %d\n", max_idx);
        fprintf(stderr, "left_amax_idx = %d\n", left_amax_idx);
        fprintf(stderr, "right_amax_idx = %d\n", right_amax_idx);
        fprintf(stderr, "*period = %f\n", *period);
    }
    ++counter;
#endif
    return RP_OK;
}

int osc_measureDutyCycle(rpApp_osc_source source, float *dutyCycle) {
    int highTime = 0;
    float meanValue;
    ECHECK_APP(osc_measureMeanVoltage(source, &meanValue));
    ECHECK_APP(scaleAmplitudeChannel(source, meanValue, &meanValue))
    for (int i = 0; i < viewSize; ++i) {
        if (view[source*viewSize + i] > meanValue) {
            ++highTime;
        }
    }
    *dutyCycle = (float)highTime / (float)viewSize;
    return RP_OK;
}

int osc_measureRootMeanSquare(rpApp_osc_source source, float *rms) {
    float rmsValue = 0;
    for (int i = 0; i < viewSize; ++i) {
        rmsValue += view[source*viewSize + i] * view[source*viewSize + i];
    }
    *rms = (float) sqrt(rmsValue / viewSize);
    return RP_OK;
}

int osc_getCursorVoltage(rpApp_osc_source source, uint32_t cursor, float *value) {
    return unscaleAmplitudeChannel(source, view[source*viewSize + cursor], value);
}

int osc_getCursorTime(uint32_t cursor, float *value) {
    if (cursor < 0 || cursor >= viewSize) {
        return RP_EOOR;
    }
    *value = viewIndexToTime(cursor);
    return RP_OK;
}

int osc_getCursorDeltaTime(uint32_t cursor1, uint32_t cursor2, float *value) {
    if (cursor1 < 0 || cursor1 >= viewSize || cursor2 < 0 || cursor2 >= viewSize) {
        return RP_EOOR;
    }
    *value = indexToTime(abs(cursor1 - cursor2));
    return RP_OK;
}

int oscGetCursorDeltaAmplitude(rpApp_osc_source source, uint32_t cursor1, uint32_t cursor2, float *value) {
    if (cursor1 < 0 || cursor1 >= viewSize || cursor2 < 0 || cursor2 >= viewSize) {
        return RP_EOOR;
    }
    float cursor1Amplitude, cursor2Amplitude;
    ECHECK_APP(osc_getCursorVoltage(source, cursor1, &cursor1Amplitude));
    ECHECK_APP(osc_getCursorVoltage(source, cursor2, &cursor2Amplitude));
    *value = (float) fabs(cursor2Amplitude - cursor1Amplitude);
    return RP_OK;
}

int osc_getCursorDeltaFrequency(uint32_t cursor1, uint32_t cursor2, float *value) {
    if (cursor1 < 0 || cursor1 >= viewSize || cursor2 < 0 || cursor2 >= viewSize) {
        return RP_EOOR;
    }
    float deltaTime;
    ECHECK_APP(osc_getCursorDeltaTime(cursor1, cursor2, &deltaTime));
    *value = 1 / deltaTime;
    return RP_OK;
}

int osc_setMathOperation(rpApp_osc_math_oper_t op) {
    operation = op;
    EXECUTE_ATOMICALLY(mutex, clearMath())
    return RP_OK;
}

int osc_getMathOperation(rpApp_osc_math_oper_t *op) {
    *op = operation;
    return RP_OK;
}

int osc_setMathSources(rp_channel_t source1, rp_channel_t source2) {
    mathSource1 = source1;
    mathSource2 = source2;
    EXECUTE_ATOMICALLY(mutex, clearMath())
    return RP_OK;
}

int osc_getMathSources(rp_channel_t *source1, rp_channel_t *source2) {
    *source1 = mathSource1;
    *source2 = mathSource2;
    return RP_OK;
}

int osc_getData(rpApp_osc_source source, float *data, uint32_t size) {
    for (int i = 0; i < size; ++i) {
        data[i] = view[source*viewSize + i];
    }
    return RP_OK;
}

int osc_setViewSize(uint32_t size) {
    viewSize = size;
    samplesPerDivision = (float) viewSize / (float) DIVISIONS_COUNT_X;
    view = realloc(view, 3 * viewSize * sizeof(float));
    if (view == NULL) {
        free(view);
        view = NULL;
        return RP_EAA;
    }
    EXECUTE_ATOMICALLY(mutex, clearView());
    return RP_OK;
}

int osc_getViewSize(uint32_t *size) {
    *size = viewSize;
    return 0;
}

/*
* Utils
*/

int threadSafe_acqStart() {
    pthread_mutex_lock(&mutex);
    ECHECK_APP_MUTEX(mutex, rp_AcqStart())
    ECHECK_APP_MUTEX(mutex, rp_AcqSetArmKeep(trigSweep != RPAPP_OSC_TRIG_SINGLE && continuousMode));
    acqRunning = true;
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

int threadSafe_acqStop() {
    pthread_mutex_lock(&mutex);
    ECHECK_APP_MUTEX(mutex, rp_AcqStop())
    ECHECK_APP_MUTEX(mutex, rp_AcqSetArmKeep(false))
    acqRunning = false;
    pthread_mutex_unlock(&mutex);
    return RP_OK;
}

float scaleAmplitude(float volts, float ampScale, float probeAtt, float ampOffset, float invertFactor) {
    return (volts * invertFactor * probeAtt + ampOffset) / ampScale;
}

float unscaleAmplitude(float value, float ampScale, float probeAtt, float ampOffset, float invertFactor) {
    return ((value * ampScale) - ampOffset) / probeAtt / invertFactor;
}

float unOffsetAmplitude(float value, float ampScale, float ampOffset) {
    return value - (ampOffset / ampScale);
}

int scaleAmplitudeChannel(rpApp_osc_source source, float volts, float *res) {
    float ampOffset, ampScale, probeAtt = 1;
    bool inverted;
    ECHECK_APP(osc_getAmplitudeOffset(source, &ampOffset));
    ECHECK_APP(osc_getAmplitudeScale(source, &ampScale));
    if (source != RPAPP_OSC_SOUR_MATH)
        ECHECK_APP(osc_getProbeAtt((rp_channel_t)source, &probeAtt));
    ECHECK_APP(osc_isInverted(source, &inverted));
    *res = scaleAmplitude(volts, ampScale, probeAtt, ampOffset, inverted ? -1 : 1);
    return RP_OK;
}

int unscaleAmplitudeChannel(rpApp_osc_source source, float value, float *res) {
    float ampOffset, ampScale, probeAtt=1;
    bool inverted;
    ECHECK_APP(osc_getAmplitudeOffset(source, &ampOffset));
    ECHECK_APP(osc_getAmplitudeScale(source, &ampScale));
    ECHECK_APP(osc_isInverted(source, &inverted));
    if (source != RPAPP_OSC_SOUR_MATH)
        ECHECK_APP(osc_getProbeAtt((rp_channel_t)source, &probeAtt));
    *res = unscaleAmplitude(value, ampScale, probeAtt, ampOffset, inverted ? -1 : 1);
    return RP_OK;
}

int unOffsetAmplitudeChannel(rpApp_osc_source source, float value, float *res) {
    float ampOffset, ampScale;
    ECHECK_APP(osc_getAmplitudeOffset(source, &ampOffset));
    ECHECK_APP(osc_getAmplitudeScale(source, &ampScale));
    *res = unOffsetAmplitude(value, ampScale, ampOffset);
    return RP_OK;
}

float viewIndexToTime(int index) {
    return indexToTime(index - viewSize / 2) + timeOffset;
}

void calculateIntegral(rp_channel_t channel, float scale, float offset, float invertFactor) {
    float dt = timeScale / samplesPerDivision;
    float v;
    ECHECK_APP_THREAD(unOffsetAmplitudeChannel((rpApp_osc_source) channel, view[channel*viewSize], &v));
    view[RPAPP_OSC_SOUR_MATH*viewSize] = v * dt;
    for (int i = 1; i < viewSize; ++i) {
        ECHECK_APP_THREAD(unOffsetAmplitudeChannel((rpApp_osc_source) channel, view[channel*viewSize + i], &v));
        view[RPAPP_OSC_SOUR_MATH*viewSize + i] = view[RPAPP_OSC_SOUR_MATH*viewSize + i-1] + (v * dt);
        view[RPAPP_OSC_SOUR_MATH*viewSize + i-1] = scaleAmplitude(view[RPAPP_OSC_SOUR_MATH*viewSize + i-1], scale, 1, offset, invertFactor);
    }
    view[RPAPP_OSC_SOUR_MATH*viewSize + viewSize-1] = scaleAmplitude(view[RPAPP_OSC_SOUR_MATH*viewSize + viewSize-1], scale, 1, offset, invertFactor);
}

void calculateDevivative(rp_channel_t channel, float scale, float offset, float invertFactor) {
    float dt2 = 2*timeScale / 1000 / samplesPerDivision;
    float v1, v2;
    ECHECK_APP_THREAD(unOffsetAmplitudeChannel((rpApp_osc_source) channel, view[channel*viewSize+1], &v1));
    ECHECK_APP_THREAD(unOffsetAmplitudeChannel((rpApp_osc_source) channel, view[channel*viewSize], &v2));
    view[RPAPP_OSC_SOUR_MATH*viewSize] = scaleAmplitude(((v1 - v2) / dt2 / 2), scale, 1, offset, invertFactor);
    for (int i = 1; i < viewSize - 1; ++i) {
        v1 = v2;
        ECHECK_APP_THREAD(unOffsetAmplitudeChannel((rpApp_osc_source) channel, view[channel*viewSize + i+1], &v2));
        view[RPAPP_OSC_SOUR_MATH*viewSize + i] = scaleAmplitude((v2 - v1) / dt2, scale, 1, offset, invertFactor);
    }
}

float calculateMath(float v1, float v2, rpApp_osc_math_oper_t op) {
    switch (op) {
        case RPAPP_OSC_MATH_ADD:
            return v1 + v2;
        case RPAPP_OSC_MATH_SUB:
            return v1 - v2;
        case RPAPP_OSC_MATH_MUL:
            return v1 * v2;
        case RPAPP_OSC_MATH_DIV:
            if (v2 != 0)
                return v1 / v2;
            else
                return v1 > 0 ? FLT_MAX * 0.9f : -FLT_MAX * 0.9f;
        case RPAPP_OSC_MATH_ABS:
            return (float) fabs(v1);
        default:
            return 0;
    }
}

float roundUpTo125(float data) {
    double power = ceil(log(data) / log(10)) - 1;       // calculate normalization factor
    double dataNorm = data / pow(10, power);            // normalize data, so that 1 < data < 10
    if (dataNorm < 2)                                   // round normalized data
        dataNorm = 2;
    else if (dataNorm < 5)
        dataNorm = 5;
    else
        dataNorm = 10;
    return (float) (dataNorm * pow(10, power));         // unnormalize data
}

void clearView() {
    int size = 3*viewSize;
    for (int i = 0; i < size; ++i) {
        view[i] = 0;
    }
    clear = true;
    auto_freRun_mode = false;
}

void clearMath() {
    for (int i = 0; i < viewSize; ++i) {
        view[RPAPP_OSC_SOUR_MATH*viewSize + i] = 0;
    }
}

int waitToFillPreTriggerBuffer(int testcancel) {
    if (continuousMode && trigSweep != RPAPP_OSC_TRIG_SINGLE) {
        return RP_OK;
    }
    float deltaSample, timeScale;
    uint32_t preTriggerCount;
    int triggerDelay;
    clock_t timer = clock();
    do {
        ECHECK_APP(rp_AcqGetTriggerDelay(&triggerDelay));
        ECHECK_APP(rp_AcqGetPreTriggerCounter(&preTriggerCount));
        ECHECK_APP(osc_getTimeScale(&timeScale));
        deltaSample = timeToIndex(timeScale) / samplesPerDivision;
        if(testcancel)
            pthread_testcancel();
    } while (preTriggerCount < viewSize/2*deltaSample - triggerDelay && clock() - timer < WAIT_TO_FILL_BUF_TIMEOUT);
    return RP_OK;
}

/*
* Thread functions
*/

void mathThreadFunction() {
    if (operation != RPAPP_OSC_MATH_NONE) {
        bool invert;
        ECHECK_APP_THREAD(osc_isInverted(RPAPP_OSC_SOUR_MATH, &invert))
        float invertFactor = invert ? -1 : 1;
        if (operation == RPAPP_OSC_MATH_DER) {
            calculateDevivative(mathSource1, math_ampScale, math_ampOffset, invertFactor);
        } else if (operation == RPAPP_OSC_MATH_INT) {
            calculateIntegral(mathSource1, math_ampScale, math_ampOffset, invertFactor);
        } else {
            float v1, v2;
            for (int i = 0; i < viewSize; ++i) {
                ECHECK_APP_THREAD(unOffsetAmplitudeChannel((rpApp_osc_source) mathSource1, view[mathSource1*viewSize + i], &v1));
                ECHECK_APP_THREAD(unOffsetAmplitudeChannel((rpApp_osc_source) mathSource2, view[mathSource2*viewSize + i], &v2));
                view[RPAPP_OSC_SOUR_MATH*viewSize + i] = scaleAmplitude(calculateMath(v1, v2, operation), math_ampScale, 1, math_ampOffset, invertFactor);
            }
        }
    }
}

void *mainThreadFun() {
    rp_acq_trig_src_t _triggerSource;
    rp_acq_trig_state_t _state;
    //clock_t _timer = clock();
    uint32_t _triggerPosition, _getBufSize, _startIndex, _writePointer = 0, _preTriggerCount = 0;
    int _triggerDelay, _preZero, _postZero;
    float _deltaSample, _timeScale;
    float data[2][ADC_BUFFER_SIZE];
    bool thisLoopAcqStart, manuallyTriggered;

    while (true) {
	pthread_testcancel();
        thisLoopAcqStart = false;

        if (clear && acqRunning) {
            ECHECK_APP_THREAD(rp_AcqSetTriggerSrc(RP_TRIG_SRC_DISABLED));
            ECHECK_APP_THREAD(threadSafe_acqStart());
            waitToFillPreTriggerBuffer(1);
            thisLoopAcqStart = false;
            ECHECK_APP_THREAD(osc_setTriggerSource(trigSource));
            EXECUTE_ATOMICALLY(mutex, auto_freRun_mode = false)
            EXECUTE_ATOMICALLY(mutex, clear = false)
        }

        ECHECK_APP_THREAD(osc_getTimeScale(&_timeScale));

        // If in auto mode end trigger timed out
        if (acqRunning && trigSweep == RPAPP_OSC_TRIG_AUTO /*&& (clock() - _timer) / CLOCKS_PER_SEC * 1000.0 > _timeScale * DIVISIONS_COUNT_X*/) {
            EXECUTE_ATOMICALLY(mutex, auto_freRun_mode = true)
        }

        ECHECK_APP_THREAD(rp_AcqGetTriggerState(&_state));
        ECHECK_APP_THREAD(rp_AcqGetTriggerSrc(&_triggerSource));

        manuallyTriggered = false;
        if (trigSweep == RPAPP_OSC_TRIG_AUTO && auto_freRun_mode /*&& _state != RP_TRIG_STATE_TRIGGERED*/) {
            threadSafe_acqStop();
            manuallyTriggered = true;
        }

        if ((_state == RP_TRIG_STATE_TRIGGERED && _timeScale >= MIN_TIME_TO_DRAW_BEFORE_TIG )
            || _triggerSource == RP_TRIG_SRC_DISABLED || manuallyTriggered) {
            // Read parameters
            ECHECK_APP_THREAD(rp_AcqGetWritePointer(&_writePointer));
            ECHECK_APP_THREAD(rp_AcqGetWritePointerAtTrig(&_triggerPosition));
            ECHECK_APP_THREAD(rp_AcqGetTriggerDelay(&_triggerDelay));
            ECHECK_APP_THREAD(rp_AcqGetPreTriggerCounter(&_preTriggerCount));

            // Calculate transformation (form data to view) parameters
            _deltaSample = timeToIndex(_timeScale) / samplesPerDivision;
            _triggerDelay = _triggerDelay % ADC_BUFFER_SIZE;

            _preZero = continuousMode ? 0 : (int) MAX(0, viewSize/2 - (_triggerDelay+_preTriggerCount)/_deltaSample);
            _postZero = (int) MAX(0, viewSize/2 - (_writePointer-(_triggerPosition+_triggerDelay))/_deltaSample);
            _startIndex = (_triggerPosition + _triggerDelay - (uint32_t) ((viewSize/2 -_preZero)*_deltaSample)) % ADC_BUFFER_SIZE;
            if (manuallyTriggered) {
                _preZero = 0;                _postZero = 0;
                _startIndex = (_writePointer - (uint32_t) ((viewSize - _preZero) * _deltaSample)) % ADC_BUFFER_SIZE;
            }
            _getBufSize = (uint32_t) ((viewSize-(_preZero + _postZero))*_deltaSample);

            // Get data
            ECHECK_APP_THREAD(rp_AcqGetDataV(RP_CH_1, _startIndex, &_getBufSize, data[0]));
            ECHECK_APP_THREAD(rp_AcqGetDataV(RP_CH_2, _startIndex, &_getBufSize, data[1]));

            if (_triggerSource == RP_TRIG_SRC_DISABLED && acqRunning) {
                if (trigSweep != RPAPP_OSC_TRIG_SINGLE) {
                    if (!continuousMode) {
                        ECHECK_APP_THREAD(threadSafe_acqStart());
                    }
                    thisLoopAcqStart = true;
                } else {
                    ECHECK_APP_THREAD(threadSafe_acqStop());
                }
            }

            // Reset autoSweep timer
            if (trigSweep == RPAPP_OSC_TRIG_AUTO) {
                //_timer = clock();
                if (!manuallyTriggered) {
                    //EXECUTE_ATOMICALLY(mutex, auto_freRun_mode = false)
                } else {
                    threadSafe_acqStart();
                    thisLoopAcqStart = true;
                }
            }

            // Write data to view buffer
            for (rp_channel_t channel = RP_CH_1; channel <= RP_CH_2; ++channel) {
                // first preZero data are wrong - from previout trigger. Last preZero data hasn't been overwritten
                for (int i = 0; i < _preZero; ++i) {
                    view[channel * viewSize + i] = 0;
                }
                for (int i = 0; i < viewSize-_postZero && (int) (i * _deltaSample) < _getBufSize; ++i) {
                    ECHECK_APP_THREAD(scaleAmplitudeChannel((rpApp_osc_source) channel, data[channel][(int) (i * _deltaSample)], view + channel*viewSize + i+_preZero));
                }
            }

            mathThreadFunction();
        }

        if (thisLoopAcqStart) {
            waitToFillPreTriggerBuffer(1);
            ECHECK_APP_THREAD(osc_setTriggerSource(trigSource));
        }
    }
}
