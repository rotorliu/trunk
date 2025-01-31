//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#include "ccShiftAndScaleCloudDlg.h"

//GUIs generated by Qt Designer
#include <ui_globalShiftAndScaleDlg.h>
#include <ui_globalShiftAndScaleAboutDlg.h>

//Local
#include "ccGlobalShiftManager.h"

//Qt
#include <QPushButton>
#include <QFile>
#include <QTextStream>
#include <QStringList>

//qCC_db
#include <ccLog.h>

//system
#include <float.h>
#include <assert.h>

//default name for the Global Shift List file
static QString s_defaultGlobalShiftListFilename("global_shift_list.txt");

//semi-persistent settings
struct LastShiftInfo : ccShiftAndScaleCloudDlg::ShiftInfo
{
	bool valid;

	LastShiftInfo()
		: ccShiftAndScaleCloudDlg::ShiftInfo("Last input")
		, valid(false)
	{}
};
static LastShiftInfo s_lastInfo;

ccShiftAndScaleCloudDlg::ccShiftAndScaleCloudDlg(const CCVector3d& Pg,
												 double Dg/*=0*/,
												 QWidget* parent/*=0*/)
	: QDialog(parent)
	, m_ui(0)
	, m_applyAll(false)
	, m_cancel(false)
	, m_activeInfoIndex(-1)
	, m_originalPoint(Pg)
	, m_originalDiagonal(Dg)
	, m_localPoint(0,0,0)
	, m_localDiagonal(-1.0)
	, m_reversedMode(false)
{
	init();

	showWarning(false);
	showKeepGlobalPosCheckbox(false);
	showScaleItems(m_originalDiagonal > 0.0);
	showCancelButton(false);
}

ccShiftAndScaleCloudDlg::ccShiftAndScaleCloudDlg(	const CCVector3d& Pl,
													double Dl,
													const CCVector3d& Pg,
													double Dg,
													QWidget* parent/*=0*/)
	: QDialog(parent)
	, m_ui(0)
	, m_applyAll(false)
	, m_cancel(false)
	, m_activeInfoIndex(-1)
	, m_originalPoint(Pg)
	, m_originalDiagonal(Dg)
	, m_localPoint(Pl)
	, m_localDiagonal(Dl)
	, m_reversedMode(true)
{
	init();

	showWarning(false);
	showTitle(false);
	showKeepGlobalPosCheckbox(true);
	showScaleItems(m_originalDiagonal > 0.0 && m_localDiagonal > 0.0);
	showCancelButton(true);

	//to update the GUI accordingly
	onGlobalPosCheckBoxToggled(m_ui->keepGlobalPosCheckBox->isChecked());
}

ccShiftAndScaleCloudDlg::~ccShiftAndScaleCloudDlg()
{
	if (m_ui)
	{
		delete m_ui;
		m_ui = 0;
	}
}

void ccShiftAndScaleCloudDlg::init()
{
	//should be called once and only once!
	if (m_ui)
	{
		assert(false);
		return;
	}

	m_ui = new Ui_GlobalShiftAndScaleDlg;
	m_ui->setupUi(this);

	m_ui->shiftX->setRange(-1.0e9,1.0e9);
	m_ui->shiftY->setRange(-1.0e9,1.0e9);
	m_ui->shiftZ->setRange(-1.0e9,1.0e9);

	updateGlobalAndLocalSystems();

	connect(m_ui->loadComboBox,				SIGNAL(currentIndexChanged(int)),	this,	SLOT(onLoadIndexChanged(int)));
	connect(m_ui->moreInfoToolButton,		SIGNAL(clicked()),					this,	SLOT(displayMoreInfo()));
	connect(m_ui->buttonBox,				SIGNAL(clicked(QAbstractButton*)),	this,	SLOT(onClick(QAbstractButton*)));
	connect(m_ui->shiftX,					SIGNAL(valueChanged(double)),		this,	SLOT(updateGlobalAndLocalSystems()));
	connect(m_ui->shiftY,					SIGNAL(valueChanged(double)),		this,	SLOT(updateGlobalAndLocalSystems()));
	connect(m_ui->shiftZ,					SIGNAL(valueChanged(double)),		this,	SLOT(updateGlobalAndLocalSystems()));
	connect(m_ui->scaleSpinBox,				SIGNAL(valueChanged(double)),		this,	SLOT(updateGlobalAndLocalSystems()));
	connect(m_ui->keepGlobalPosCheckBox,	SIGNAL(toggled(bool)),				this,	SLOT(onGlobalPosCheckBoxToggled(bool)));
}

void ccShiftAndScaleCloudDlg::displayMoreInfo()
{
	QDialog dlg(this);
	Ui_GlobalShiftAndScaleAboutDlg uiDlg;
	uiDlg.setupUi(&dlg);

	dlg.exec();

}

bool ccShiftAndScaleCloudDlg::addFileInfo()
{
	//try to load the 'global_shift_list.txt" file
	return loadInfoFromFile(QApplication::applicationDirPath() + QString("/")+ s_defaultGlobalShiftListFilename);
}

bool ccShiftAndScaleCloudDlg::loadInfoFromFile(QString filename)
{
	QFile file(filename);
	if (!file.open(QFile::Text | QFile::ReadOnly))
		return false;

	size_t originalSize = m_defaultInfos.size();

	QTextStream stream(&file);
	unsigned lineNumber = 0;

	while (true)
	{
		//read next line
		QString line = stream.readLine();
		if (line.isEmpty())
			break;
		++lineNumber;

		if (line.startsWith("//"))
			continue;
		
		//split line in 5 items
		QStringList tokens = line.split(";",QString::SkipEmptyParts);
		if (tokens.size() != 5)
		{
			//invalid file
			ccLog::Warning(QString("[ccShiftAndScaleCloudDlg::loadInfoFromFile] File '%1' is malformed (5 items expected per line)").arg(filename));
			m_defaultInfos.resize(originalSize);
			return false;
		}

		//decode items
		bool ok = true;
		unsigned errors = 0;
		ShiftInfo info;
		info.name = tokens[0].trimmed();
		info.shift.x = tokens[1].toDouble(&ok);
		if (!ok) ++errors;
		info.shift.y = tokens[2].toDouble(&ok);
		if (!ok) ++errors;
		info.shift.z = tokens[3].toDouble(&ok);
		if (!ok) ++errors;
		info.scale = tokens[4].toDouble(&ok);
		if (!ok) ++errors;
		
		//process errors
		if (errors)
		{
			//invalid file
			ccLog::Warning(QString("[ccShiftAndScaleCloudDlg::loadInfoFromFile] File '%1' is malformed (wrong item type encountered on line %2)").arg(filename).arg(lineNumber));
			m_defaultInfos.resize(originalSize);
			return false;
		}

		try
		{
			m_defaultInfos.push_back(info);
		}
		catch (const std::bad_alloc&)
		{
			//not enough memory
			ccLog::Warning(QString("[ccShiftAndScaleCloudDlg::loadInfoFromFile] Not enough memory to read file '%1'").arg(filename));
			m_defaultInfos.resize(originalSize);
			return false;
		}
	}
	
	//now add the new entries in the combo-box
	for (size_t i=originalSize; i<m_defaultInfos.size(); ++i)
		m_ui->loadComboBox->addItem(m_defaultInfos[i].name);
	m_ui->loadComboBox->setEnabled(m_defaultInfos.size() >= 2);

	return true;
}

void ccShiftAndScaleCloudDlg::updateGlobalAndLocalSystems()
{
	updateGlobalSystem();
	updateLocalSystem();
}

bool AlmostEq(double a, double b)
{
	qint64 ai = static_cast<qint64>(a * 100.0);
	qint64 bi = static_cast<qint64>(b * 100.0);

	return ai == bi;
}

void ccShiftAndScaleCloudDlg::updateGlobalSystem()
{
	CCVector3d P = m_originalPoint;
	double diag = m_originalDiagonal;
	if (m_reversedMode && !keepGlobalPos())
	{
		P = (m_localPoint - getShift()) / getScale();
		diag = m_localDiagonal / getScale();
	}

	m_ui->xOriginLabel->setText(QString("x = %1").arg(P.x,0,'f'));
	m_ui->xOriginLabel->setStyleSheet(AlmostEq(P.x,m_originalPoint.x) ? QString() : QString("color: purple;"));
	m_ui->yOriginLabel->setText(QString("y = %1").arg(P.y,0,'f'));
	m_ui->yOriginLabel->setStyleSheet(AlmostEq(P.y,m_originalPoint.y) ? QString() : QString("color: purple;"));
	m_ui->zOriginLabel->setText(QString("z = %1").arg(P.z,0,'f'));
	m_ui->zOriginLabel->setStyleSheet(AlmostEq(P.z,m_originalPoint.z) ? QString() : QString("color: purple;"));

	m_ui->diagOriginLabel->setText(QString("diagonal = %1").arg(diag,0,'f'));
	m_ui->diagOriginLabel->setStyleSheet(AlmostEq(diag,m_originalDiagonal) ? QString() : QString("color: purple;"));
}

void ccShiftAndScaleCloudDlg::updateLocalSystem()
{
	CCVector3d	localPoint = m_localPoint;
	double localDiagonal = m_localDiagonal;
	if (!m_reversedMode || keepGlobalPos())
	{
		localPoint = (m_originalPoint + getShift())  * getScale();
		localDiagonal = m_originalDiagonal * getScale();
	}

	//adaptive precision
	double maxCoord = std::max(fabs(localPoint.x),fabs(localPoint.y));
	maxCoord = std::max(fabs(localPoint.z),maxCoord);
	int digitsBeforeDec = static_cast<int>(floor(log10(maxCoord)))+1;
	int prec = std::max(0,8-digitsBeforeDec);

	m_ui->xDestLabel->setText(QString("x = %1").arg(localPoint.x,0,'f',prec));
	m_ui->xDestLabel->setStyleSheet(ccGlobalShiftManager::NeedShift(localPoint.x) ? QString("color: red;") : QString() );
	m_ui->yDestLabel->setText(QString("y = %1").arg(localPoint.y,0,'f',prec));
	m_ui->yDestLabel->setStyleSheet(ccGlobalShiftManager::NeedShift(localPoint.y) ? QString("color: red;") : QString() );
	m_ui->zDestLabel->setText(QString("z = %1").arg(localPoint.z,0,'f',prec));
	m_ui->zDestLabel->setStyleSheet(ccGlobalShiftManager::NeedShift(localPoint.z) ? QString("color: red;") : QString() );

	m_ui->diagDestLabel->setText(QString("diagonal = %1").arg(localDiagonal,0,'f',prec));
	m_ui->diagDestLabel->setStyleSheet(ccGlobalShiftManager::NeedRescale(localDiagonal) ? QString("color: red;") : QString() );
}

void ccShiftAndScaleCloudDlg::setShift(const CCVector3d& shift)
{
	m_ui->shiftX->setValue(shift.x);
	m_ui->shiftY->setValue(shift.y);
	m_ui->shiftZ->setValue(shift.z);
}

CCVector3d ccShiftAndScaleCloudDlg::getShift() const
{
	return CCVector3d( m_ui->shiftX->value(), m_ui->shiftY->value(), m_ui->shiftZ->value() );
}

void ccShiftAndScaleCloudDlg::setScale(double scale)
{
	m_ui->scaleSpinBox->setValue(scale);
}

double ccShiftAndScaleCloudDlg::getScale() const
{
	return m_ui->scaleSpinBox->value();
}

void ccShiftAndScaleCloudDlg::showScaleItems(bool state)
{
	m_ui->diagOriginLabel->setVisible(state);
	m_ui->diagDestLabel->setVisible(state);
	//m_ui->scaleFrame->setVisible(state);
}

void ccShiftAndScaleCloudDlg::showApplyAllButton(bool state)
{
	m_ui->buttonBox->button(QDialogButtonBox::YesToAll)->setVisible(state);
}

void ccShiftAndScaleCloudDlg::showApplyButton(bool state)
{
	m_ui->buttonBox->button(QDialogButtonBox::Yes)->setVisible(state);
}

void ccShiftAndScaleCloudDlg::showNoButton(bool state)
{
	m_ui->buttonBox->button(QDialogButtonBox::No)->setVisible(state);
}

void ccShiftAndScaleCloudDlg::showCancelButton(bool state)
{
	m_ui->buttonBox->button(QDialogButtonBox::Cancel)->setVisible(state);
}

void ccShiftAndScaleCloudDlg::showWarning(bool state)
{
	m_ui->warningLabel->setVisible(state);
}

void ccShiftAndScaleCloudDlg::showTitle(bool state)
{
	m_ui->titleFrame->setVisible(state);
}

void ccShiftAndScaleCloudDlg::showKeepGlobalPosCheckbox(bool state)
{
	m_ui->keepGlobalPosCheckBox->setVisible(state);
}

bool ccShiftAndScaleCloudDlg::keepGlobalPos() const
{
	return m_ui->keepGlobalPosCheckBox->isChecked();
}

void ccShiftAndScaleCloudDlg::setKeepGlobalPos(bool state)
{
	m_ui->keepGlobalPosCheckBox->setChecked(state);
}

void ccShiftAndScaleCloudDlg::onGlobalPosCheckBoxToggled(bool state)
{
	//set the thickest border to the point that will be modified
	m_ui->smallCubeFrame->setLineWidth(state  ? 2 : 1);
	m_ui->bigCubeFrame->setLineWidth(state ? 1 : 2);

	updateGlobalSystem();
	updateLocalSystem();
}

void ccShiftAndScaleCloudDlg::onClick(QAbstractButton* button)
{
	bool saveInfo = false;
	m_applyAll = false;
	m_cancel = false;

	if (button == m_ui->buttonBox->button(QDialogButtonBox::Yes))
	{
		saveInfo = true;
	}
	else if (button == m_ui->buttonBox->button(QDialogButtonBox::YesToAll))
	{
		saveInfo = true;
		m_applyAll = true;
	}
	else if (button == m_ui->buttonBox->button(QDialogButtonBox::Cancel))
	{
		m_cancel = true;
	}

	if (saveInfo)
	{
		s_lastInfo.valid = true;
		s_lastInfo.shift = getShift();
		s_lastInfo.scale = getScale();
	}
}

void ccShiftAndScaleCloudDlg::onLoadIndexChanged(int index)
{
	if (index < 0 || index >= static_cast<int>(m_defaultInfos.size()))
		return;

	setShift(m_defaultInfos[index].shift);
	if (m_ui->scaleSpinBox->isVisible())
		setScale(m_defaultInfos[index].scale);
}

bool ccShiftAndScaleCloudDlg::getLast(ShiftInfo& info) const
{
	info = s_lastInfo;
	return s_lastInfo.valid;
}

bool ccShiftAndScaleCloudDlg::getInfo(size_t index, ShiftInfo& info) const
{
	if (index >= m_defaultInfos.size())
		return false;

	info = m_defaultInfos[index];

	return true;
}

void ccShiftAndScaleCloudDlg::setCurrentProfile(int index)
{
	if (index >= 0 && index < static_cast<int>(m_defaultInfos.size()))
	{
		m_ui->loadComboBox->setCurrentIndex(index);
	}
}

int ccShiftAndScaleCloudDlg::addShiftInfo(const ShiftInfo& info)
{
	try
	{
		m_defaultInfos.push_back(info);
	}
	catch (const std::bad_alloc&)
	{
		//not enough memory
		return -1;
	}

	m_ui->loadComboBox->addItem(m_defaultInfos.back().name);
	m_ui->loadComboBox->setEnabled(m_defaultInfos.size() >= 2);

	return static_cast<int>(m_defaultInfos.size())-1;
}
