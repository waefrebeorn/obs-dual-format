#pragma once

#include <QFrame>
#include <QHBoxLayout>

#include "dual-output-const.h"

class DualOutputTitle : public QFrame {
	Q_OBJECT

public:
	DualOutputTitle(QWidget *parent = nullptr);

public slots:
	void onPlatformChanged(const QString &uuid,
			       ChannelData::ChannelDualOutput outputType);

	void addPlatformIcon(const QString &uuid, bool bMain);
	bool removePlatformIcon(QHBoxLayout *layout, const QString &uuid);
	void removePlatformIcon(const QString &uuid);

	void initPlatformIcon();

	void showHorizontalDisplay(bool);
	void showVerticalDisplay(bool);

protected:
	bool isIconExists(QHBoxLayout *layout, const QString &uuid);

private:
	QPushButton *m_buttonHPreview = nullptr;
	QPushButton *m_buttonVPreview = nullptr;

	QWidget *m_widgetHPlatform = nullptr;
	QWidget *m_widgetVPlatform = nullptr;

	QHBoxLayout *m_layoutHPlatform = nullptr;
	QHBoxLayout *m_layoutVPlatform = nullptr;

	QStringList m_slRemovedPlatform;
};
