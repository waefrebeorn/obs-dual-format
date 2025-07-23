#include "DualOutputHandler.hpp"

#include <qmetaobject.h>

#include "window-basic-main.hpp"
#include "platform.hpp"

DualOutputHandler::DualOutputHandler() {}

DualOutputHandler::~DualOutputHandler() {}

DualOutputHandler::operator bool() const
{
	if (!houtput) {
		return false;
	} else if (!is_dual_output_on()) {
		return true;
	} else if (!voutput) {
		return false;
	} else {
		return true;
	}
}

bool DualOutputHandler::operator==(std::nullptr_t) const
{
	return !operator bool();
}

BasicOutputHandler *DualOutputHandler::operator->()
{
	return houtput.get();
}

const BasicOutputHandler *DualOutputHandler::operator->() const
{
	return houtput.get();
}

void DualOutputHandler::reset(OBSBasic *main_)
{
	main = main_;
	houtput.reset();
	voutput.reset();
}

void DualOutputHandler::reset(bool advOut, OBSBasic *main)
{
	reset(main);
	houtput.reset(advOut ? CreateAdvancedOutputHandler(main, this, false)
			     : CreateSimpleOutputHandler(main, this, false));
	if (is_dual_output_on()) {
		voutput.reset(
			advOut ? CreateAdvancedOutputHandler(main, this, true)
			       : CreateSimpleOutputHandler(main, this, true));
	}
}

void DualOutputHandler::resetState()
{
	startStreaming[Horizontal] = startStreaming[Vertical] = false;
	streamDelayStarting[Horizontal] = streamDelayStarting[Vertical] = false;
	streamDelayStopping[Horizontal] = streamDelayStopping[Vertical] = false;
	streamingStartInvoked = false;
	streamingStart[Horizontal] = streamingStart[Vertical] = false;
	streamStopping[Horizontal] = streamStopping[Vertical] = false;
	streamingStop[Horizontal] = streamingStop[Vertical] = false;
	streamDelayStartingSec[Horizontal] = streamDelayStartingSec[Vertical] =
		0;
	streamDelayStoppingSec[Horizontal] = streamDelayStoppingSec[Vertical] =
		0;
	streamingStopErrorCode[Horizontal] = streamingStopErrorCode[Vertical] =
		0;
	streamingStopLastError[Horizontal].clear();
	streamingStopLastError[Vertical].clear();
}

std::pair<std::shared_future<void>, std::shared_future<void>>
DualOutputHandler::SetupStreaming(obs_service_t *service,
				  SetupStreamingContinuation_t continuation,
				  obs_service_t *vservice,
				  SetupStreamingContinuation_t vcontinuation)
{
	return houtput->SetupStreaming(service, continuation, vservice,
				       vcontinuation);
}

bool DualOutputHandler::StartStreaming(obs_service_t *service,
				       obs_service_t *vservice)
{
	resetState();

	if (nullptr == service) {
		startStreaming[Vertical] = voutput->StartStreaming(vservice);
	} else if (nullptr == vservice) {
		startStreaming[Horizontal] = houtput->StartStreaming(service);
	} else {
		startStreaming[Vertical] = voutput->StartStreaming(vservice);
		startStreaming[Horizontal] = houtput->StartStreaming(service);
	}

	return startStreaming[Horizontal] || startStreaming[Vertical];
}

bool DualOutputHandler::StartRecording()
{
	return houtput->StartRecording();
}

bool DualOutputHandler::StartReplayBuffer()
{
	return houtput->StartReplayBuffer();
}

bool DualOutputHandler::StartVirtualCam()
{
	return houtput->StartVirtualCam();
}

void DualOutputHandler::StopStreaming(bool force, StreamingType streamType)
{
	if (StreamingType::Horizontal == streamType ||
	    StreamingType::StreamingTypeMax == streamType) {
		houtput->StopStreaming(force);
	}
	if (is_dual_output_on()) {
		if (StreamingType::Vertical == streamType ||
		    StreamingType::StreamingTypeMax == streamType) {
			voutput->StopStreaming(force);
		}
	}
}

void DualOutputHandler::StopRecording(bool force)
{
	houtput->StopRecording(force);
}

void DualOutputHandler::StopReplayBuffer(bool force)
{
	houtput->StopReplayBuffer(force);
}

void DualOutputHandler::StopVirtualCam()
{
	houtput->StopVirtualCam();
}

bool DualOutputHandler::StreamingActive() const
{
	return houtput->StreamingActive() ||
	       (voutput && voutput->StreamingActive());
}

bool DualOutputHandler::RecordingActive() const
{
	return houtput->RecordingActive();
}

bool DualOutputHandler::ReplayBufferActive() const
{
	return houtput->ReplayBufferActive();
}

bool DualOutputHandler::VirtualCamActive() const
{
	return houtput->VirtualCamActive();
}

void DualOutputHandler::Update()
{
	houtput->Update();
}

void DualOutputHandler::UpdateVirtualCamOutputSource()
{
	houtput->UpdateVirtualCamOutputSource();
}

bool DualOutputHandler::Active() const
{
	return houtput->Active() || (voutput && voutput->Active());
}

bool DualOutputHandler::streamingActive() const
{
	return houtput->streamingActive ||
	       (voutput && voutput->streamingActive);
}

bool DualOutputHandler::streamingActive(StreamingType streamType) const
{
	switch (streamType) {
	case StreamingType::Horizontal:
		return houtput->streamingActive;
	case StreamingType::Vertical:
		return voutput && voutput->streamingActive;
	default:
		assert(false || "It's unexpected");
		return false;
	}
}

bool DualOutputHandler::replayBufferActive() const
{
	return houtput->replayBufferActive;
}

bool DualOutputHandler::virtualCamActive() const
{
	return houtput->virtualCamActive;
}

void DualOutputHandler::StreamDelayStarting(BasicOutputHandler *handler, int sec)
{
	auto type = houtput.get() == handler ? StreamingType::Horizontal
					     : StreamingType::Vertical;
	streamDelayStarting[type] = true;
	streamDelayStartingSec[type] = sec;
	if ((!startStreaming[Horizontal] || streamDelayStarting[Horizontal]) &&
	    (!startStreaming[Vertical] || streamDelayStarting[Vertical])) {
		QMetaObject::invokeMethod(
			main, "StreamDelayStarting",
			Q_ARG(int, streamDelayStartingSec[Horizontal]),
			Q_ARG(int, streamDelayStartingSec[Vertical]));
	}
}

void DualOutputHandler::StreamDelayStopping(BasicOutputHandler *handler, int sec)
{
	auto type = houtput.get() == handler ? StreamingType::Horizontal
					     : StreamingType::Vertical;
	streamDelayStopping[type] = true;
	streamDelayStoppingSec[type] = sec;
	if ((!startStreaming[Horizontal] || streamDelayStopping[Horizontal]) &&
	    (!startStreaming[Vertical] || streamDelayStopping[Vertical])) {
		QMetaObject::invokeMethod(
			main, "StreamDelayStopping",
			Q_ARG(int, streamDelayStoppingSec[Horizontal]),
			Q_ARG(int, streamDelayStoppingSec[Vertical]));
	}
}

void DualOutputHandler::StreamingStart(BasicOutputHandler *handler)
{
	auto type = houtput.get() == handler ? StreamingType::Horizontal
					     : StreamingType::Vertical;
	streamingStart[type] = true;
	if ((!streamingStartInvoked) &&
	    (!startStreaming[Horizontal] || streamingStart[Horizontal] ||
	     streamingStop[Horizontal]) &&
	    (!startStreaming[Vertical] || streamingStart[Vertical] ||
	     streamingStop[Vertical])) {
		streamingStartInvoked = true;

		if (streamingStop[Horizontal]) {
			main->SysTrayNotify(
				QTStr("Stream.Error"),
				QSystemTrayIcon::Warning);
			streamingStopErrorCode[StreamingType::Horizontal] = 0;
			streamingStopLastError[StreamingType::Horizontal]
				.clear();
		} else if (streamingStop[Vertical]) {
			main->SysTrayNotify(
				QTStr("Stream.Error"),
				QSystemTrayIcon::Warning);
			streamingStopErrorCode[StreamingType::Vertical] = 0;
			streamingStopLastError[StreamingType::Vertical]
				.clear();
		}

		QMetaObject::invokeMethod(main, "StreamingStart");
	}
}

void DualOutputHandler::StreamStopping(BasicOutputHandler *handler)
{
	auto type = houtput.get() == handler ? StreamingType::Horizontal
					     : StreamingType::Vertical;
	streamStopping[type] = true;
	if ((!startStreaming[Horizontal] || streamStopping[Horizontal]) &&
	    (!startStreaming[Vertical] || streamStopping[Vertical])) {
		QMetaObject::invokeMethod(main, "StreamStopping");
	}
}

void DualOutputHandler::StreamingStop(BasicOutputHandler *handler, int errorcode,
				      QString last_error)
{
	auto type = houtput.get() == handler ? StreamingType::Horizontal
					     : StreamingType::Vertical;
	streamingStop[type] = true;
	streamingStopErrorCode[type] = errorcode;
	streamingStopLastError[type] = last_error;

	if ((!streamingStartInvoked) &&
	    (streamingStart[Horizontal] || streamingStart[Vertical]) &&
	    (!startStreaming[Horizontal] || streamingStart[Horizontal] ||
	     streamingStop[Horizontal]) &&
	    (!startStreaming[Vertical] || streamingStart[Vertical] ||
	     streamingStop[Vertical])) {
		streamingStartInvoked = true;

		main->SysTrayNotify(QTStr("Stream.Error"),
				    QSystemTrayIcon::Warning);
		streamingStopErrorCode[type] = 0;
		streamingStopLastError[type].clear();

		QMetaObject::invokeMethod(main, "StreamingStart");
	}

	if ((!startStreaming[Horizontal] || streamingStop[Horizontal]) &&
	    (!startStreaming[Vertical] || streamingStop[Vertical])) {
		QMetaObject::invokeMethod(
			main, "StreamingStop",
			Q_ARG(int, streamingStopErrorCode[Horizontal]),
			Q_ARG(QString, streamingStopLastError[Horizontal]),
			Q_ARG(int, streamingStopErrorCode[Vertical]),
			Q_ARG(QString, streamingStopLastError[Vertical]));
	}
}

void DualOutputHandler::RecordingStart(BasicOutputHandler *handler)
{
	QMetaObject::invokeMethod(main, "RecordingStart");
}

void DualOutputHandler::RecordStopping(BasicOutputHandler *handler)
{
	QMetaObject::invokeMethod(main, "RecordStopping");
}

void DualOutputHandler::RecordingStop(BasicOutputHandler *handler, int code,
				      QString last_error)
{
	QMetaObject::invokeMethod(main, "RecordingStop", Q_ARG(int, code),
				  Q_ARG(QString, last_error));
}

void DualOutputHandler::RecordingFileChanged(BasicOutputHandler *handler,
					     QString lastRecordingPath)
{
	QMetaObject::invokeMethod(main, "RecordingFileChanged",
				  Q_ARG(QString, lastRecordingPath));
}

void DualOutputHandler::ReplayBufferStart(BasicOutputHandler *handler)
{
	QMetaObject::invokeMethod(main, "ReplayBufferStart");
}

void DualOutputHandler::ReplayBufferSaved(BasicOutputHandler *handler)
{
	QMetaObject::invokeMethod(main, "ReplayBufferSaved");
}

void DualOutputHandler::ReplayBufferStopping(BasicOutputHandler *handler)
{
	QMetaObject::invokeMethod(main, "ReplayBufferStopping");
}

void DualOutputHandler::ReplayBufferStop(BasicOutputHandler *handler, int code)
{
	QMetaObject::invokeMethod(main, "ReplayBufferStop", Q_ARG(int, code));
}

void DualOutputHandler::OnVirtualCamStart(BasicOutputHandler *handler)
{
	QMetaObject::invokeMethod(main, "OnVirtualCamStart");
}

void DualOutputHandler::OnVirtualCamStop(BasicOutputHandler *handler, int code)
{
	QMetaObject::invokeMethod(main, "OnVirtualCamStop", Q_ARG(int, code));
}
