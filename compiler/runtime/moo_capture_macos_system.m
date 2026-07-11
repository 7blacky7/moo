#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreAudio/CoreAudio.h>
#import <AvailabilityMacros.h>
#include "moo_capture_pull_internal.h"
#include <mach/mach_time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
#define MOO_AUDIO_ELEMENT_MAIN kAudioObjectPropertyElementMain
#else
#define MOO_AUDIO_ELEMENT_MAIN kAudioObjectPropertyElementMaster
#endif

static void set_error(char *out, size_t cap, NSString *message) {
    if (!out || cap == 0) return;
    const char *text = message ? message.UTF8String : "unbekannter macOS-Capture-Fehler";
    snprintf(out, cap, "%s", text ? text : "unbekannter macOS-Capture-Fehler");
}

static int64_t system_clock_ms(void) {
    static mach_timebase_info_data_t info;
    if (info.denom == 0) mach_timebase_info(&info);
    uint64_t ticks = mach_continuous_time();
    long double nanos = (long double)ticks * info.numer / info.denom;
    return (int64_t)(nanos / 1000000.0L);
}

static bool request_permission(AVMediaType type, NSString *usage, char *error, size_t cap) {
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:type];
    if (status == AVAuthorizationStatusAuthorized) return true;
    if (status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted) {
        set_error(error, cap, [NSString stringWithFormat:
            @"%@-Zugriff wurde durch macOS/TCC verweigert; Berechtigung in Systemeinstellungen erteilen",
            usage]);
        return false;
    }
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block BOOL granted = NO;
    [AVCaptureDevice requestAccessForMediaType:type completionHandler:^(BOOL ok) {
        granted = ok;
        dispatch_semaphore_signal(done);
    }];
    if (dispatch_semaphore_wait(done,
            dispatch_time(DISPATCH_TIME_NOW, 10LL * NSEC_PER_SEC)) != 0) {
        set_error(error, cap, [NSString stringWithFormat:
            @"%@-Berechtigungsdialog antwortete nicht innerhalb von 10 Sekunden", usage]);
        return false;
    }
    if (!granted) {
        set_error(error, cap, [NSString stringWithFormat:
            @"%@-Zugriff wurde im Berechtigungsdialog verweigert", usage]);
        return false;
    }
    return true;
}

@interface MooMacCamera : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
@public
    AVCaptureSession *_session;
    AVCaptureVideoDataOutput *_output;
    dispatch_queue_t _queue;
    dispatch_semaphore_t _event;
    NSLock *_lock;
    NSData *_latest;
    int32_t _width;
    int32_t _height;
    int32_t _stride;
    int64_t _timestamp100ns;
    BOOL _stopped;
}
- (void)close;
@end

@implementation MooMacCamera
- (instancetype)init {
    if ((self = [super init])) {
        _queue = dispatch_queue_create("org.moolang.capture.camera", DISPATCH_QUEUE_SERIAL);
        _event = dispatch_semaphore_create(0);
        _lock = [[NSLock alloc] init];
    }
    return self;
}
- (void)captureOutput:(AVCaptureOutput *)output
 didOutputSampleBuffer:(CMSampleBufferRef)sample
        fromConnection:(AVCaptureConnection *)connection {
    (void)output; (void)connection;
    CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
    if (!image || CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
        return;
    size_t width = CVPixelBufferGetWidth(image);
    size_t height = CVPixelBufferGetHeight(image);
    size_t stride = CVPixelBufferGetBytesPerRow(image);
    void *base = CVPixelBufferGetBaseAddress(image);
    size_t bytes = CVPixelBufferGetDataSize(image);
    NSData *copy = nil;
    if (base && width > 0 && height > 0 && stride >= width * 4 &&
        stride <= SIZE_MAX / height && stride * height <= bytes) {
        copy = [NSData dataWithBytes:base length:stride * height];
    }
    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
    int64_t stamp = CMTIME_IS_NUMERIC(pts) && pts.timescale > 0
        ? (int64_t)((long double)pts.value * 10000000.0L / pts.timescale) : 0;
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    if (!copy) return;

    [_lock lock];
    if (!_stopped) {
        _latest = copy;
        _width = (int32_t)width;
        _height = (int32_t)height;
        _stride = (int32_t)stride;
        _timestamp100ns = stamp;
        dispatch_semaphore_signal(_event);
    }
    [_lock unlock];
}
- (void)close {
    [_lock lock];
    if (_stopped) { [_lock unlock]; return; }
    _stopped = YES;
    [_lock unlock];
    [_output setSampleBufferDelegate:nil queue:NULL];
    [_session stopRunning];
    dispatch_sync(_queue, ^{});
    [_lock lock];
    _latest = nil;
    [_lock unlock];
}
@end

@interface MooMacMicrophone : NSObject {
@public
    AVAudioEngine *_engine;
    AVAudioInputNode *_input;
    dispatch_queue_t _queue;
    dispatch_semaphore_t _event;
    NSLock *_lock;
    NSMutableData *_pending;
    id _configurationObserver;
    int32_t _rate;
    int32_t _channels;
    int32_t _period;
    int32_t _buffer;
    BOOL _recoverable;
    BOOL _stopped;
}
- (BOOL)start:(NSError **)error;
- (void)close;
@end

static int32_t input_period_frames(void) {
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof device;
    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        MOO_AUDIO_ELEMENT_MAIN
    };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL,
                                   &size, &device) != noErr ||
        device == kAudioObjectUnknown) return 1024;
    UInt32 frames = 0;
    size = sizeof frames;
    address.mSelector = kAudioDevicePropertyBufferFrameSize;
    address.mScope = kAudioDevicePropertyScopeInput;
    if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &frames) != noErr ||
        frames == 0 || frames > INT32_MAX) return 1024;
    return (int32_t)frames;
}

@implementation MooMacMicrophone
- (instancetype)init {
    if ((self = [super init])) {
        _queue = dispatch_queue_create("org.moolang.capture.microphone", DISPATCH_QUEUE_SERIAL);
        _event = dispatch_semaphore_create(0);
        _lock = [[NSLock alloc] init];
        _pending = [[NSMutableData alloc] init];
    }
    return self;
}
- (BOOL)start:(NSError **)error {
    _engine = [[AVAudioEngine alloc] init];
    _input = _engine.inputNode;
    AVAudioFormat *format = [_input inputFormatForBus:0];
    if (!format || format.sampleRate <= 0 || format.channelCount < 1 || format.channelCount > 2) {
        if (error) *error = [NSError errorWithDomain:@"org.moolang.capture" code:1
            userInfo:@{NSLocalizedDescriptionKey:@"CoreAudio lieferte kein unterstütztes Float-Mono/Stereo-Format"}];
        return NO;
    }
    _rate = (int32_t)llround(format.sampleRate);
    _channels = (int32_t)format.channelCount;
    _period = input_period_frames();
    _buffer = _period <= INT32_MAX / 8 ? _period * 8 : INT32_MAX;
    __weak MooMacMicrophone *weakSelf = self;
    [_input installTapOnBus:0 bufferSize:(AVAudioFrameCount)_period format:format
        block:^(AVAudioPCMBuffer *buffer, AVAudioTime *when) {
        (void)when;
        MooMacMicrophone *strongSelf = weakSelf;
        if (!strongSelf || !buffer.floatChannelData || buffer.frameLength == 0) return;
        int32_t channels = strongSelf->_channels;
        size_t frames = buffer.frameLength;
        if (frames > SIZE_MAX / ((size_t)channels * sizeof(float))) return;
        size_t count = frames * (size_t)channels;
        float *copy = malloc(count * sizeof(float));
        if (!copy) return;
        for (size_t f = 0; f < frames; ++f)
            for (int32_t c = 0; c < channels; ++c)
                copy[f * (size_t)channels + (size_t)c] = buffer.floatChannelData[c][f];
        dispatch_async(strongSelf->_queue, ^{
            [strongSelf->_lock lock];
            if (!strongSelf->_stopped) {
                size_t maxBytes = (size_t)strongSelf->_buffer * channels * sizeof(float);
                size_t incoming = count * sizeof(float);
                if (incoming >= maxBytes) {
                    [strongSelf->_pending setData:
                        [NSData dataWithBytes:(uint8_t *)copy + incoming - maxBytes length:maxBytes]];
                } else {
                    size_t old = strongSelf->_pending.length;
                    if (old + incoming > maxBytes) {
                        size_t drop = old + incoming - maxBytes;
                        [strongSelf->_pending replaceBytesInRange:NSMakeRange(0, drop)
                            withBytes:NULL length:0];
                    }
                    [strongSelf->_pending appendBytes:copy length:incoming];
                }
                dispatch_semaphore_signal(strongSelf->_event);
            }
            [strongSelf->_lock unlock];
            free(copy);
        });
    }];
    if (![_engine startAndReturnError:error]) {
        [_input removeTapOnBus:0];
        return NO;
    }
    _configurationObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:AVAudioEngineConfigurationChangeNotification object:_engine
        queue:nil usingBlock:^(NSNotification *note) {
            (void)note;
            MooMacMicrophone *strongSelf = weakSelf;
            if (!strongSelf) return;
            [strongSelf->_lock lock];
            if (!strongSelf->_stopped) {
                strongSelf->_recoverable = YES;
                dispatch_semaphore_signal(strongSelf->_event);
            }
            [strongSelf->_lock unlock];
        }];
    return YES;
}
- (void)close {
    [_lock lock];
    if (_stopped) { [_lock unlock]; return; }
    _stopped = YES;
    [_lock unlock];
    if (_configurationObserver)
        [[NSNotificationCenter defaultCenter] removeObserver:_configurationObserver];
    [_input removeTapOnBus:0];
    [_engine stop];
    dispatch_sync(_queue, ^{});
    [_lock lock];
    [_pending setLength:0];
    [_lock unlock];
}
@end

static bool system_startup(char *error, size_t cap) {
    (void)error; (void)cap;
    return true;
}
static void system_shutdown(void) {}

static NSArray<AVCaptureDevice *> *camera_devices(void) {
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
    AVCaptureDeviceType external_type = AVCaptureDeviceTypeExternal;
#else
    AVCaptureDeviceType external_type = AVCaptureDeviceTypeExternalUnknown;
#endif
    AVCaptureDeviceDiscoverySession *discovery =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[
            AVCaptureDeviceTypeBuiltInWideAngleCamera, external_type
        ] mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionUnspecified];
    return discovery.devices;
}

static MooPullResult system_camera_enumerate(MooPullCameraInfo *out, int32_t cap,
        int32_t *total, char *error, size_t error_cap) {
    @autoreleasepool {
        NSArray<AVCaptureDevice *> *devices = camera_devices();
        if (devices.count > INT32_MAX) {
            set_error(error, error_cap, @"macOS meldete zu viele Kameras");
            return MOO_PULL_ERROR;
        }
        *total = (int32_t)devices.count;
        int32_t n = *total < cap ? *total : cap;
        for (int32_t i = 0; i < n; ++i) {
            AVCaptureDevice *device = devices[(NSUInteger)i];
            snprintf(out[i].id, sizeof out[i].id, "%s", device.uniqueID.UTF8String ?: "");
            snprintf(out[i].name, sizeof out[i].name, "%s", device.localizedName.UTF8String ?: "");
        }
        return MOO_PULL_OK;
    }
}

static MooPullResult system_camera_open(const char *id, int32_t width, int32_t height,
        double fps, bool exact, void **session, int32_t *actual_width,
        int32_t *actual_height, double *actual_fps, int32_t *queue_bound,
        char *error, size_t error_cap) {
    @autoreleasepool {
        if (!request_permission(AVMediaTypeVideo, @"Kamera", error, error_cap))
            return MOO_PULL_ERROR;
        AVCaptureDevice *device = nil;
        NSArray<AVCaptureDevice *> *devices = camera_devices();
        if (id && id[0]) {
            NSString *wanted = [NSString stringWithUTF8String:id];
            for (AVCaptureDevice *candidate in devices)
                if ([candidate.uniqueID isEqualToString:wanted]) { device = candidate; break; }
        }
        if (!device) device = devices.firstObject;
        if (!device) {
            set_error(error, error_cap, @"kamera_oeffnen: keine Kamera gefunden");
            return MOO_PULL_ERROR;
        }

        NSError *nsError = nil;
        AVCaptureDeviceFormat *chosen = nil;
        AVFrameRateRange *chosenRange = nil;
        for (AVCaptureDeviceFormat *format in device.formats) {
            CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
            for (AVFrameRateRange *range in format.videoSupportedFrameRateRanges) {
                BOOL geometry = dims.width == width && dims.height == height;
                BOOL rate = fps >= range.minFrameRate && fps <= range.maxFrameRate;
                if (geometry && rate) { chosen = format; chosenRange = range; break; }
            }
            if (chosen) break;
        }
        if (!chosen && exact) {
            set_error(error, error_cap, @"kamera_oeffnen: exakt angeforderte Auflösung/FPS nicht verfügbar");
            return MOO_PULL_ERROR;
        }
        if (!chosen) {
            chosen = device.activeFormat;
            chosenRange = chosen.videoSupportedFrameRateRanges.firstObject;
        }
        if (![device lockForConfiguration:&nsError]) {
            set_error(error, error_cap, nsError.localizedDescription);
            return MOO_PULL_ERROR;
        }
        device.activeFormat = chosen;
        if (chosenRange && fps > 0) {
            double selected = MIN(MAX(fps, chosenRange.minFrameRate), chosenRange.maxFrameRate);
            CMTime duration = CMTimeMakeWithSeconds(1.0 / selected, 1000000);
            device.activeVideoMinFrameDuration = duration;
            device.activeVideoMaxFrameDuration = duration;
        }
        [device unlockForConfiguration];

        MooMacCamera *camera = [[MooMacCamera alloc] init];
        camera->_session = [[AVCaptureSession alloc] init];
        AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&nsError];
        if (!input || ![camera->_session canAddInput:input]) {
            set_error(error, error_cap, nsError.localizedDescription ?: @"Kameraeingang nicht verfügbar");
            return MOO_PULL_ERROR;
        }
        [camera->_session addInput:input];
        camera->_output = [[AVCaptureVideoDataOutput alloc] init];
        camera->_output.alwaysDiscardsLateVideoFrames = YES;
        camera->_output.videoSettings = @{
            (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };
        if (![camera->_session canAddOutput:camera->_output]) {
            set_error(error, error_cap, @"BGRA-Kameraausgang kann nicht hinzugefügt werden");
            return MOO_PULL_ERROR;
        }
        [camera->_session addOutput:camera->_output];
        [camera->_output setSampleBufferDelegate:camera queue:camera->_queue];
        [camera->_session startRunning];

        CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(device.activeFormat.formatDescription);
        *actual_width = dims.width;
        *actual_height = dims.height;
        CMTime duration = device.activeVideoMinFrameDuration;
        *actual_fps = CMTIME_IS_NUMERIC(duration) && duration.value > 0
            ? (double)duration.timescale / (double)duration.value : fps;
        *queue_bound = 1;
        *session = (__bridge_retained void *)camera;
        return MOO_PULL_OK;
    }
}

static MooPullResult system_camera_wait(void *session, int32_t timeout,
        char *error, size_t cap) {
    (void)error; (void)cap;
    MooMacCamera *camera = (__bridge MooMacCamera *)session;
    long result = dispatch_semaphore_wait(camera->_event,
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout * NSEC_PER_MSEC));
    return result == 0 ? MOO_PULL_OK : MOO_PULL_TIMEOUT;
}
static MooPullResult system_camera_next(void *session, MooPullFramePacket *packet,
        char *error, size_t cap) {
    (void)error; (void)cap;
    MooMacCamera *camera = (__bridge MooMacCamera *)session;
    [camera->_lock lock];
    NSData *data = camera->_latest;
    if (!data) { [camera->_lock unlock]; return MOO_PULL_EMPTY; }
    size_t length = data.length;
    uint8_t *copy = malloc(length);
    if (!copy) { [camera->_lock unlock]; return MOO_PULL_ERROR; }
    memcpy(copy, data.bytes, length);
    packet->bgra = copy;
    packet->bytes = length;
    packet->width = camera->_width;
    packet->height = camera->_height;
    packet->stride = camera->_stride;
    packet->timestamp_100ns = camera->_timestamp100ns;
    camera->_latest = nil;
    [camera->_lock unlock];
    return MOO_PULL_OK;
}
static void system_camera_release(MooPullFramePacket *packet) {
    free(packet->bgra);
    memset(packet, 0, sizeof *packet);
}
static void system_camera_close(void *session) {
    if (!session) return;
    MooMacCamera *camera = CFBridgingRelease(session);
    [camera close];
}

static MooPullResult system_microphone_open(const char *id, int32_t rate,
        int32_t channels, void **session, int32_t *actual_rate,
        int32_t *actual_channels, int32_t *period_frames, int32_t *buffer_frames,
        char *error, size_t error_cap) {
    @autoreleasepool {
        if (id && id[0] && strcmp(id, "default") != 0) {
            set_error(error, error_cap,
                @"AVAudioEngine-v1 unterstützt nur das Standard-Eingabegerät");
            return MOO_PULL_ERROR;
        }
        if (!request_permission(AVMediaTypeAudio, @"Mikrofon", error, error_cap))
            return MOO_PULL_ERROR;
        MooMacMicrophone *microphone = [[MooMacMicrophone alloc] init];
        NSError *nsError = nil;
        if (![microphone start:&nsError]) {
            set_error(error, error_cap, nsError.localizedDescription);
            [microphone close];
            return MOO_PULL_ERROR;
        }
        (void)rate; (void)channels;
        *actual_rate = microphone->_rate;
        *actual_channels = microphone->_channels;
        *period_frames = microphone->_period;
        *buffer_frames = microphone->_buffer;
        *session = (__bridge_retained void *)microphone;
        return MOO_PULL_OK;
    }
}
static MooPullResult system_microphone_wait(void *session, int32_t timeout,
        char *error, size_t cap) {
    (void)error; (void)cap;
    MooMacMicrophone *microphone = (__bridge MooMacMicrophone *)session;
    [microphone->_lock lock];
    BOOL recoverable = microphone->_recoverable;
    [microphone->_lock unlock];
    if (recoverable) return MOO_PULL_RECOVERABLE;
    long result = dispatch_semaphore_wait(microphone->_event,
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout * NSEC_PER_MSEC));
    if (result != 0) return MOO_PULL_TIMEOUT;
    [microphone->_lock lock];
    recoverable = microphone->_recoverable;
    [microphone->_lock unlock];
    return recoverable ? MOO_PULL_RECOVERABLE : MOO_PULL_OK;
}
static MooPullResult system_microphone_next(void *session, MooPullAudioPacket *packet,
        char *error, size_t cap) {
    (void)error; (void)cap;
    MooMacMicrophone *microphone = (__bridge MooMacMicrophone *)session;
    [microphone->_lock lock];
    size_t bytes = microphone->_pending.length;
    if (bytes == 0) { [microphone->_lock unlock]; return MOO_PULL_EMPTY; }
    void *copy = malloc(bytes);
    if (!copy) { [microphone->_lock unlock]; return MOO_PULL_ERROR; }
    memcpy(copy, microphone->_pending.bytes, bytes);
    [microphone->_pending setLength:0];
    int32_t channels = microphone->_channels;
    [microphone->_lock unlock];
    packet->samples = copy;
    packet->frames = (int32_t)(bytes / ((size_t)channels * sizeof(float)));
    packet->channels = channels;
    packet->flags = 0;
    packet->token = copy;
    return MOO_PULL_OK;
}
static void system_microphone_release(MooPullAudioPacket *packet) {
    free(packet->token);
    memset(packet, 0, sizeof *packet);
}
static MooPullResult system_microphone_recover(void *session, char *error, size_t cap) {
    MooMacMicrophone *microphone = (__bridge MooMacMicrophone *)session;
    /* Eine Route-/Formatänderung invalidiert InputNode und Tap. Vollständig
     * abbauen, Queue drainieren und mit dem neu ausgehandelten Format starten. */
    [microphone close];
    [microphone->_lock lock];
    microphone->_stopped = NO;
    microphone->_recoverable = NO;
    [microphone->_pending setLength:0];
    [microphone->_lock unlock];
    NSError *nsError = nil;
    if (![microphone start:&nsError]) {
        set_error(error, cap, nsError.localizedDescription);
        return MOO_PULL_ERROR;
    }
    return MOO_PULL_OK;
}
static void system_microphone_close(void *session) {
    if (!session) return;
    MooMacMicrophone *microphone = CFBridgingRelease(session);
    [microphone close];
}

static const MooCapturePullOps system_ops = {
    system_clock_ms, system_startup, system_shutdown,
    system_camera_enumerate, system_camera_open, system_camera_wait,
    system_camera_next, system_camera_release, system_camera_close,
    system_microphone_open, system_microphone_wait, system_microphone_next,
    system_microphone_release, system_microphone_recover, system_microphone_close
};
const MooCapturePullOps *moo_capture_pull_system_ops(void) { return &system_ops; }
