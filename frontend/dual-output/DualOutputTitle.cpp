#include "DualOutputTitle.h"

#include <QStackedLayout>
#include <QLabel>
#include <QPushButton>

#include "window-basic-main.hpp"
#include "platform.hpp"

using namespace std;

DualOutputTitle::DualOutputTitle(QWidget *parent) : QFrame(parent)
{
	setFixedHeight(40);

	auto stackedLayout = new QStackedLayout(this);
	stackedLayout->setStackingMode(QStackedLayout::StackAll);

	auto widgetOuter = new QWidget();
	auto layoutOuter = new QHBoxLayout(widgetOuter);
	layoutOuter->setContentsMargins(QMargins());
	stackedLayout->addWidget(widgetOuter);

	m_buttonHPreview = new QPushButton();
	m_buttonHPreview->setObjectName("buttonHPreview");
	m_buttonHPreview->setCheckable(true);
	m_buttonHPreview->setChecked(true);
	m_buttonHPreview->setToolTip(
		tr("DualOutput.Preview.Title.Horizontal.Tip.Hide"));
	layoutOuter->addWidget(m_buttonHPreview);

	m_buttonVPreview = new QPushButton();
	m_buttonVPreview->setObjectName("buttonVPreview");
	m_buttonVPreview->setCheckable(true);
	m_buttonVPreview->setChecked(
		OBSBasic::instance()->getVerticalPreviewEnabled());
	m_buttonVPreview->setToolTip(
		tr("DualOutput.Preview.Title.Vertical.Tip.Hide"));
	layoutOuter->addWidget(m_buttonVPreview);

	layoutOuter->addStretch(1);

	auto buttonSetting =
		new QRadioButton(tr("DualOutput.Preview.Title.Settings"));
	buttonSetting->setObjectName("buttonSetting");
	buttonSetting->setCheckable(false);
	buttonSetting->setLayoutDirection(Qt::RightToLeft);
	layoutOuter->addWidget(buttonSetting);

	auto widgetInner = new QWidget();
	auto layoutInner = new QHBoxLayout(widgetInner);
	layoutInner->setContentsMargins(QMargins());
	stackedLayout->addWidget(widgetInner);

	m_widgetHPlatform = new QWidget();
	layoutInner->addWidget(m_widgetHPlatform);

	auto iconHPlatform = new QLabel();
	iconHPlatform->setObjectName("iconHPlatform");

	m_layoutHPlatform = new QHBoxLayout(m_widgetHPlatform);
	m_layoutHPlatform->setContentsMargins(QMargins());
	m_layoutHPlatform->addStretch();
	m_layoutHPlatform->addWidget(iconHPlatform);
	m_layoutHPlatform->addWidget(
		new QLabel(tr("DualOutput.Preview.Title.Horizontal")));
	m_layoutHPlatform->addStretch();

	m_widgetVPlatform = new QWidget();
	m_widgetVPlatform->setVisible(
		OBSBasic::instance()->getVerticalPreviewEnabled());
	layoutInner->addWidget(m_widgetVPlatform);

	auto iconVPlatform = new QLabel();
	iconVPlatform->setObjectName("iconVPlatform");

	m_layoutVPlatform = new QHBoxLayout(m_widgetVPlatform);
	m_layoutVPlatform->setContentsMargins(QMargins());
	m_layoutVPlatform->addStretch();
	m_layoutVPlatform->addWidget(iconVPlatform);
	m_layoutVPlatform->addWidget(
		new QLabel(tr("DualOutput.Preview.Title.Vertical")));
	m_layoutVPlatform->addStretch();

	connect(m_buttonHPreview, &QPushButton::toggled, OBSBasic::instance(),
		&OBSBasic::showHorizontalDisplay);
	connect(m_buttonVPreview, &QPushButton::toggled, OBSBasic::instance(),
		&OBSBasic::showVerticalDisplay);

	connect(buttonSetting, &QPushButton::clicked, this, [] {
		OBSBasic::instance()->showSettingView(QString("Video"),
						      QString());
	});
}

void DualOutputTitle::showHorizontalDisplay(bool bVisible)
{
	m_widgetHPlatform->setVisible(bVisible);
	m_buttonHPreview->setToolTip(
		bVisible ? tr("DualOutput.Preview.Title.Horizontal.Tip.Hide")
			 : tr("DualOutput.Preview.Title.Horizontal.Tip.Show"));

	QSignalBlocker signalBlocker(m_buttonHPreview);
	m_buttonHPreview->setChecked(bVisible);
}

void DualOutputTitle::showVerticalDisplay(bool bVisible)
{
	m_widgetVPlatform->setVisible(bVisible);
	m_buttonVPreview->setToolTip(
		bVisible ? tr("DualOutput.Preview.Title.Vertical.Tip.Hide")
			 : tr("DualOutput.Preview.Title.Vertical.Tip.Show"));

	QSignalBlocker signalBlocker(m_buttonVPreview);
	m_buttonVPreview->setChecked(bVisible);
}
