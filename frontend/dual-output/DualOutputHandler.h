#pragma once

#include <QObject>
#include <memory>
#include <future>

#include "BasicOutputHandler.hpp"

class DualOutputHandler : public QObject {
	Q_OBJECT

public:
	std::unique_ptr<BasicOutputHandler> houtput;
	std::unique_ptr<BasicOutputHandler> voutput;

	DualOutputHandler();
	~DualOutputHandler();

	operator bool() const;
	bool operator==(std::nullptr_t) const;
	BasicOutputHandler *operator->();
	const BasicOutputHandler *operator->() const;

	void reset(OBSBasic *main);
	void reset(bool advOut, OBSBasic *main);
	void resetState();

	std::pair<std::shared_future<void>, std::shared_future<void>>
	SetupStreaming(obs_service_t *service,
		       SetupStreamingContinuation_t continuation,
		       obs_service_t *vservice = nullptr,
		       SetupStreamingContinuation_t vcontinuation = nullptr);
	bool StartStreaming(obs_service_t *service,
			    obs_service_t *vservice = nullptr);
	bool StartRecording();
	bool StartReplayBuffer();
	bool StartVirtualCam();
	void StopStreaming(bool force = false,
			   StreamingType streamType = StreamingTypeMax);
	void StopRecording(bool force = false);
	void StopReplayBuffer(bool force = false);
	void StopVirtualCam();
	bool StreamingActive() const;
	bool RecordingActive() const;
	bool ReplayBufferActive() const;
	bool VirtualCamActive() const;

	void Update();

	void UpdateVirtualCamOutputSource();

	bool Active() const;
	bool streamingActive() const;
	bool streamingActive(StreamingType streamType) const;
	bool replayBufferActive() const;
	bool virtualCamActive() const;
private slots:
	void StreamDelayStarting(BasicOutputHandler *handler, int sec);
	void StreamDelayStopping(BasicOutputHandler *handler, int sec);

	void StreamingStart(BasicOutputHandler *handler);
	void StreamStopping(BasicOutputHandler *handler);
	void StreamingStop(BasicOutputHandler *handler, int errorcode,
			   QString last_error);

	void RecordingStart(BasicOutputHandler *handler);
	void RecordStopping(BasicOutputHandler *handler);
	void RecordingStop(BasicOutputHandler *handler, int code,
			   QString last_error);
	void RecordingFileChanged(BasicOutputHandler *handler,
				  QString lastRecordingPath);

	void ReplayBufferStart(BasicOutputHandler *handler);
	void ReplayBufferSaved(BasicOutputHandler *handler);
	void ReplayBufferStopping(BasicOutputHandler *handler);
	void ReplayBufferStop(BasicOutputHandler *handler, int code);

	void OnVirtualCamStart(BasicOutputHandler *handler);
	void OnVirtualCamStop(BasicOutputHandler *handler, int code);
};
