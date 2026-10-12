// Native ScreenCaptureKit bridge for AI-Tune.
//
// stdout protocol: little-endian UInt32 frame count, followed by interleaved
// Float32 stereo samples. Logs always go to stderr so Python can safely parse
// the stream. The packaged application must bundle the compiled executable.

import AVFoundation
import CoreMedia
import Foundation
import ScreenCaptureKit

final class SystemAudioCapture: NSObject, SCStreamOutput, SCStreamDelegate {
    private var stream: SCStream?
    private let outputQueue = DispatchQueue(label: "com.truyenhuu.aitune.system-audio", qos: .userInteractive)

    func start() async throws {
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        guard let display = content.displays.first else {
            throw NSError(domain: "AI-Tune", code: 1, userInfo: [NSLocalizedDescriptionKey: "No display available"])
        }

        // Capturing the active display is the least surprising native route:
        // ScreenCaptureKit provides its system-audio stream once the user has
        // granted macOS Screen Recording permission.
        let filter = SCContentFilter(display: display, excludingApplications: [], exceptingWindows: [])
        let configuration = SCStreamConfiguration()
        configuration.capturesAudio = true
        configuration.excludesCurrentProcessAudio = true
        configuration.sampleRate = 48_000
        configuration.channelCount = 2
        configuration.width = 2
        configuration.height = 2
        configuration.minimumFrameInterval = CMTime(value: 1, timescale: 1)

        let stream = SCStream(filter: filter, configuration: configuration, delegate: self)
        try stream.addStreamOutput(self, type: .audio, sampleHandlerQueue: outputQueue)
        self.stream = stream
        try await stream.startCapture()
        fputs("AI-Tune system audio capture running\n", stderr)
    }

    func stop() async {
        try? await stream?.stopCapture()
        stream = nil
    }

    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of outputType: SCStreamOutputType) {
        guard outputType == .audio, CMSampleBufferDataIsReady(sampleBuffer) else { return }
        guard let format = CMSampleBufferGetFormatDescription(sampleBuffer),
              let asbdPointer = CMAudioFormatDescriptionGetStreamBasicDescription(format) else { return }
        let asbd = asbdPointer.pointee
        guard asbd.mBitsPerChannel == 32 else { return }

        let listSize = MemoryLayout<AudioBufferList>.size + MemoryLayout<AudioBuffer>.size * 7
        let rawList = UnsafeMutableRawPointer.allocate(byteCount: listSize, alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { rawList.deallocate() }
        let list = rawList.assumingMemoryBound(to: AudioBufferList.self)
        var retainedBlock: CMBlockBuffer?
        let result = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
            sampleBuffer,
            bufferListSizeNeededOut: nil,
            bufferListOut: list,
            bufferListSize: listSize,
            blockBufferAllocator: nil,
            blockBufferMemoryAllocator: nil,
            flags: kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
            blockBufferOut: &retainedBlock
        )
        guard result == noErr else { return }

        let buffers = UnsafeMutableAudioBufferListPointer(list)
        guard let first = buffers.first, let firstData = first.mData else { return }
        let bytesPerSample = MemoryLayout<Float>.size
        let channels = max(1, Int(asbd.mChannelsPerFrame))
        let frames = Int(first.mDataByteSize) / (bytesPerSample * max(1, channels))
        guard frames > 0 else { return }

        var packetFrames = UInt32(frames).littleEndian
        FileHandle.standardOutput.write(Data(bytes: &packetFrames, count: MemoryLayout<UInt32>.size))
        if buffers.count == 1 && channels >= 2 {
            FileHandle.standardOutput.write(Data(bytes: firstData, count: frames * bytesPerSample * 2))
            return
        }

        // ScreenCaptureKit may return non-interleaved buffers. Interleave the
        // first two channels for the Python consumer while preserving timing.
        let left = firstData.assumingMemoryBound(to: Float.self)
        let right = buffers.count > 1 && buffers[1].mData != nil
            ? buffers[1].mData!.assumingMemoryBound(to: Float.self) : left
        var interleaved = [Float](repeating: 0, count: frames * 2)
        for frame in 0..<frames {
            interleaved[frame * 2] = left[frame]
            interleaved[frame * 2 + 1] = right[frame]
        }
        interleaved.withUnsafeBytes { FileHandle.standardOutput.write(Data($0)) }
    }

    func stream(_ stream: SCStream, didStopWithError error: Error) {
        fputs("AI-Tune system audio capture stopped: \(error.localizedDescription)\n", stderr)
        exit(2)
    }
}

@main
struct Main {
    static func main() async {
        let capture = SystemAudioCapture()
        do {
            try await capture.start()
            try? await Task.sleep(nanoseconds: UInt64.max)
        } catch {
            fputs("AI-Tune system audio capture error: \(error.localizedDescription)\n", stderr)
            exit(1)
        }
    }
}
