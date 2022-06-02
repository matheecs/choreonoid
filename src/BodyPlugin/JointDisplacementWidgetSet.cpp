#include "JointDisplacementWidgetSet.h"
#include "BodySelectionManager.h"
#include "BodyItem.h"
#include <cnoid/DisplayValueFormat>
#include <cnoid/Body>
#include <cnoid/Link>
#include <cnoid/Archive>
#include <cnoid/EigenUtil>
#include <cnoid/Buttons>
#include <cnoid/SpinBox>
#include <cnoid/Slider>
#include <cnoid/Dial>
#include <cnoid/LazyCaller>
#include <cnoid/MenuManager>
#include <cnoid/MathUtil>
#include <QLabel>
#include <QKeyEvent>
#include <QStyle>
#include "gettext.h"

using namespace std;
using namespace cnoid;

namespace {

/**
   Slider resolution.
   This value should not be increased more because QDial won't work correctly in that case.
*/
const double resolution = 100000.0;

const QString normalStyle("font-weight: normal");
const QString warningStyle("font-weight: bold; color: red");
const QString handleNormalStyle("");
const QString handleWarningStyle("QSlider::handle:horizontal {background-color: red;}");

class JointIndicator
{
public:
    JointDisplacementWidgetSet::Impl* baseImpl;
    QWidget* baseWidget;
    int index;
    Link* joint;
    double unitConversionRatio;
    DoubleSpinBox spin;
    Slider slider;
    Dial dial;
    bool isWarningState;
    SpinBox phaseSpin;
    bool hasPhases;
    int minPhase;
    int maxPhase;
    QLabel idLabel;
    QLabel nameLabel;
    QLabel lowerLimitLabel;
    QLabel upperLimitLabel;

    JointIndicator(JointDisplacementWidgetSet::Impl* baseImpl, int index);
    void setVisible(bool on);
    int attachTo(QGridLayout* grid, int row, int col, bool overlapJointName);
    void setNextTabOrderIndicator(JointIndicator* next);
    bool setRangeLabelValue(QLabel& label, double value, bool isInfinite, int precision);
    void initialize(Link* joint);
    void updateDisplacement(bool forceUpdate);
    int getCurrentPhase();
    void onDisplacementInput(double value);
    void onPhaseInput(int phase);
    void removeWidgetsFrom(QGridLayout* grid);
};

}

namespace cnoid {

class JointDisplacementWidgetSet::Impl : public QObject
{
public:
    JointDisplacementWidgetSet* self;

    QWidget* baseWidget;
    QGridLayout* grid;
    QGridLayout* sharedGrid;
    int* sharedRowCounter;
    int currentRowSize;
    QLabel* targetBodyLabel;

    BodySelectionManager* bodySelectionManager;
    BodyItemPtr currentBodyItem;
    vector<int> activeJointLinkIndices;
    vector<JointIndicator*> jointIndicators;
    LazyCaller updateJointDisplacementsLater;
            
    ScopedConnection linkSelectionChangeConnection;
    ScopedConnection kinematicStateChangeConnection;

    DisplayValueFormat* dvFormat;
    ScopedConnection dvFormatConnection;
    int lengthUnit;
    int lengthDecimals;
    double defaultMaxLength;
    double lengthStep;
    int angleUnit;
    int angleDecimals;
    double defaultMaxAngle;
    double angleStep;

    bool isSelectedJointsOnlyMode;
    bool isPrivateJointEnabled;
    bool isJointIdVisible;
    bool isJointNameVisible;
    bool isOverlapJointNameMode;
    bool isSliderEnabled;
    bool isDialEnabled;
    bool isPhaseEnabled;
    bool isRangeLimitMode;

    Signal<void(QWidget* widget)> sigJointWidgetFocused;

    Impl(JointDisplacementWidgetSet* self, QWidget* baseWidget, QGridLayout* sharedGrid, int* sharedRowCounter);
    ~Impl();
    void updateTargetBodyLabel();
    void setOptionMenuTo(MenuManager& menu);
    void setBodyItem(BodyItem* bodyItem);
    void updateIndicatorGrid();
    void initializeIndicators(int num);
    virtual bool eventFilter(QObject* object, QEvent* event) override;
    bool onSliderKeyPressEvent(Slider* slider, QKeyEvent* event);
    void focusSlider(int index);
    bool onDialKeyPressEvent(Dial* dial, QKeyEvent* event);
    void focusDial(int index);
    void onOperationFinished();
    void notifyJointDisplacementInput();
    void updateJointDisplacements();
    bool storeState(Archive* archive);
    bool restoreState(const Archive* archive);
};

}


JointDisplacementWidgetSet::JointDisplacementWidgetSet
(QWidget* baseWidget, QGridLayout* sharedGrid, int* sharedRowCounter)
{
    impl = new Impl(this, baseWidget, sharedGrid, sharedRowCounter);
}


JointDisplacementWidgetSet::Impl::Impl
(JointDisplacementWidgetSet* self, QWidget* baseWidget, QGridLayout* sharedGrid, int* sharedRowCounter)
    : self(self),
      baseWidget(baseWidget),
      sharedGrid(sharedGrid),
      sharedRowCounter(sharedRowCounter)
{
    if(sharedGrid){
        grid = sharedGrid;

    } else {
        auto style = baseWidget->style();
        int lmargin = style->pixelMetric(QStyle::PM_LayoutLeftMargin);
        int rmargin = style->pixelMetric(QStyle::PM_LayoutRightMargin);
        int hspacing = style->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
        int vspacing = style->pixelMetric(QStyle::PM_LayoutVerticalSpacing);
        auto vbox = new QVBoxLayout;
        vbox->setContentsMargins(lmargin, 0, rmargin, 0);
        baseWidget->setLayout(vbox);
        grid = new QGridLayout;
        grid->setHorizontalSpacing(hspacing / 2);
        grid->setVerticalSpacing(vspacing / 2);
        vbox->addLayout(grid);
        vbox->addStretch();
    }

    targetBodyLabel = nullptr;
    
    currentRowSize = 0;

    isSelectedJointsOnlyMode = false;
    isPrivateJointEnabled = false;
    isJointIdVisible = false;
    isJointNameVisible = true;
    isOverlapJointNameMode = false;
    isSliderEnabled = true;
    isDialEnabled = false;
    isPhaseEnabled = true;
    isRangeLimitMode = false;

    dvFormat = DisplayValueFormat::instance();
    dvFormatConnection =
        dvFormat->sigFormatChanged().connect(
            [&](){ updateIndicatorGrid(); });
    
    updateIndicatorGrid();

    updateJointDisplacementsLater.setFunction([&](){ updateJointDisplacements(); });
    updateJointDisplacementsLater.setPriority(LazyCaller::PRIORITY_LOW);

    bodySelectionManager = BodySelectionManager::instance();
}


JointDisplacementWidgetSet::~JointDisplacementWidgetSet()
{
    delete impl;
}


JointDisplacementWidgetSet::Impl::~Impl()
{
    if(targetBodyLabel){
        delete targetBodyLabel;
    }
    for(size_t i=0; i < jointIndicators.size(); ++i){
        delete jointIndicators[i];
    }
    if(!sharedGrid){
        delete grid;
    }
}


void JointDisplacementWidgetSet::setTargetBodyLabelEnabled(bool on)
{
    bool current = (bool)impl->targetBodyLabel;
    if(on != current){
        if(on){
            impl->targetBodyLabel = new QLabel;
            impl->targetBodyLabel->setStyleSheet("font-weight: bold");
            impl->updateTargetBodyLabel();
        } else {
            delete impl->targetBodyLabel;
            impl->targetBodyLabel = nullptr;
        }
    }
}


void JointDisplacementWidgetSet::Impl::updateTargetBodyLabel()
{
    if(currentBodyItem){
        targetBodyLabel->setText(currentBodyItem->displayName().c_str());
    } else {
        targetBodyLabel->setText("------");
    }
}


void JointDisplacementWidgetSet::setVisible(bool on)
{
    if(impl->targetBodyLabel){
        impl->targetBodyLabel->setVisible(on);
    }
    for(auto& indicator : impl->jointIndicators){
        indicator->setVisible(on);
    }
}


void JointDisplacementWidgetSet::setOptionMenuTo(MenuManager& menu)
{
    impl->setOptionMenuTo(menu);
}


void JointDisplacementWidgetSet::Impl::setOptionMenuTo(MenuManager& menu)
{
    auto selectedJointsOnlyCheck = menu.addCheckItem(_("Selected joints only"));
    selectedJointsOnlyCheck->setChecked(isSelectedJointsOnlyMode);
    selectedJointsOnlyCheck->sigToggled().connect(
        [&](bool on){ isSelectedJointsOnlyMode = on; updateIndicatorGrid(); });

    auto privateJointCheck = menu.addCheckItem(_("Show private joints"));
    privateJointCheck->setChecked(isPrivateJointEnabled);
    privateJointCheck->sigToggled().connect(
        [&](bool on){ isPrivateJointEnabled = on; updateIndicatorGrid(); });

    auto jointIdCheck = menu.addCheckItem(_("Joint ID"));
    jointIdCheck->setChecked(isJointIdVisible);
    jointIdCheck->sigToggled().connect(
        [&](bool on){ isJointIdVisible = on; updateIndicatorGrid(); });

    auto jointNameCheck = menu.addCheckItem(_("Joint name"));
    jointNameCheck->setChecked(isJointNameVisible);
    jointNameCheck->sigToggled().connect(
        [&](bool on){ isJointNameVisible = on; updateIndicatorGrid(); });

    auto overlapJointNameCheck = menu.addCheckItem(_("Overlap joint name"));
    overlapJointNameCheck->setChecked(isOverlapJointNameMode);
    overlapJointNameCheck->sigToggled().connect(
        [&](bool on){ isOverlapJointNameMode = on; updateIndicatorGrid(); });

    auto sliderCheck = menu.addCheckItem(_("Slider"));
    sliderCheck->setChecked(isSliderEnabled);
    sliderCheck->sigToggled().connect(
        [&](bool on){ isSliderEnabled = on; updateIndicatorGrid(); });

    auto dialCheck = menu.addCheckItem(_("Dial"));
    dialCheck->setChecked(isDialEnabled);
    dialCheck->sigToggled().connect(
        [&](bool on){ isDialEnabled = on; updateIndicatorGrid(); });

    auto phaseCheck = menu.addCheckItem(_("Phase"));
    phaseCheck->setChecked(isPhaseEnabled);
    phaseCheck->sigToggled().connect(
        [&](bool on){ isPhaseEnabled = on; updateIndicatorGrid(); });

    auto rangeLimitCheck = menu.addCheckItem(_("Limit the slider range to within +/- 360 deg."));
    rangeLimitCheck->setChecked(isRangeLimitMode);
    rangeLimitCheck->sigToggled().connect(
        [&](bool on){ isRangeLimitMode = on; updateIndicatorGrid(); });
}


void JointDisplacementWidgetSet::setBodyItem(BodyItem* bodyItem)
{
    impl->setBodyItem(bodyItem);
}


void JointDisplacementWidgetSet::Impl::setBodyItem(BodyItem* bodyItem)
{
    if(bodyItem == currentBodyItem){
        if(sharedRowCounter){
            *sharedRowCounter += currentRowSize;
        }
    } else {
        linkSelectionChangeConnection.disconnect();
        kinematicStateChangeConnection.disconnect();
        currentBodyItem = bodyItem;
        if(targetBodyLabel){
            updateTargetBodyLabel();
        }
        updateIndicatorGrid();

        if(bodyItem){
            linkSelectionChangeConnection =
                bodySelectionManager->sigLinkSelectionChanged(bodyItem).connect(
                    [&](const std::vector<bool>&){ updateIndicatorGrid(); });
            
            kinematicStateChangeConnection =
                bodyItem->sigKinematicStateChanged().connect(updateJointDisplacementsLater);
            updateJointDisplacements();
        }
    }
}


BodyItem* JointDisplacementWidgetSet::bodyItem()
{
    return impl->currentBodyItem;
}


void JointDisplacementWidgetSet::Impl::updateIndicatorGrid()
{
    if(!currentBodyItem){
        initializeIndicators(0);
        return;
    }

    BodyPtr body = currentBodyItem->body();
    auto& linkSelection = bodySelectionManager->linkSelection(currentBodyItem);
    activeJointLinkIndices.clear();
    auto& allJoints = body->allJoints();
    int numJoints = isPrivateJointEnabled ? allJoints.size() : body->numJoints();
    for(size_t i=0; i < numJoints; ++i){
        auto joint = allJoints[i];
        if(joint->isValid()){
            int linkIndex = joint->index();
            if(!isSelectedJointsOnlyMode || linkSelection[linkIndex]){
                activeJointLinkIndices.push_back(linkIndex);
            }
        }
    }
    
    int n = activeJointLinkIndices.size();
    initializeIndicators(n);

    lengthUnit = dvFormat->lengthUnit();
    if(lengthUnit == DisplayValueFormat::Millimeter){
        defaultMaxLength = 10000.0;
    } else {
        defaultMaxLength = 10.0;
    }
    lengthDecimals = dvFormat->lengthDecimals();
    defaultMaxLength -= pow(10.0, -lengthDecimals);
    lengthStep = dvFormat->lengthStep();
    
    angleUnit = dvFormat->angleUnit();
    if(angleUnit == DisplayValueFormat::Degree){
        defaultMaxAngle = 36000.0;
    } else {
        defaultMaxAngle = 314.0;
    }
    angleDecimals = dvFormat->angleDecimals();
    defaultMaxAngle -= pow(10.0, -angleDecimals);
    angleStep = dvFormat->angleStep();
    
    int row;
    if(sharedRowCounter){
        row = *sharedRowCounter;
    } else {
        row = 0;
    }

    if(targetBodyLabel){
        grid->addWidget(targetBodyLabel, row++, 0, 1, 6);
        targetBodyLabel->show();
    }
    
    for(int i=0; i < n; ++i){
        auto indicator = jointIndicators[i];
        indicator->initialize(body->link(activeJointLinkIndices[i]));
        row = indicator->attachTo(grid, row, 0, isOverlapJointNameMode);
        indicator->setVisible(true);
    }
    for(int i=0; i < n - 1; ++i){
        jointIndicators[i]->setNextTabOrderIndicator(jointIndicators[i+1]);
    }

    if(sharedRowCounter){
        currentRowSize = row - *sharedRowCounter;
        *sharedRowCounter = row;
    } else {
        currentRowSize = row;
    }
}


void JointDisplacementWidgetSet::Impl::initializeIndicators(int num)
{
    int prevNum = jointIndicators.size();

    for(int i=0; i < prevNum; ++i){
        jointIndicators[i]->setVisible(false);
    }

    if(num > prevNum){
        for(int i=prevNum; i < num; ++i){
            int index = jointIndicators.size();
            jointIndicators.push_back(new JointIndicator(this, index));
        }
    } else if(num < prevNum){
        for(int i=num; i < prevNum; ++i){
            delete jointIndicators[i];
        }
        jointIndicators.resize(num);
    }
}


namespace {

JointIndicator::JointIndicator(JointDisplacementWidgetSet::Impl* baseImpl, int index)
    : baseImpl(baseImpl),
      baseWidget(baseImpl->baseWidget),
      index(index),
      joint(nullptr),
      spin(baseWidget),
      slider(Qt::Horizontal, baseWidget),
      isWarningState(false),
      dial(baseWidget),
      phaseSpin(baseWidget),
      idLabel(baseWidget),
      nameLabel(baseWidget),
      lowerLimitLabel(baseWidget),
      upperLimitLabel(baseWidget)
{
    idLabel.setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    nameLabel.setAlignment(Qt::AlignCenter);
    nameLabel.setTextInteractionFlags(Qt::TextSelectableByMouse);
    lowerLimitLabel.setAlignment(Qt::AlignCenter);
    upperLimitLabel.setAlignment(Qt::AlignCenter);
    
    spin.setAlignment(Qt::AlignCenter);
    spin.setUndoRedoKeyInputEnabled(true);
    spin.sigValueChanged().connect(
        [=](double v){ onDisplacementInput(v); });
    spin.sigEditingFinishedWithValueChange().connect(
        [=](){ baseImpl->onOperationFinished(); });
    
    slider.setSingleStep(0.1 * resolution);
    slider.setProperty("JointSliderIndex", index);
    slider.installEventFilter(baseImpl);
    slider.sigValueChanged().connect(
        [=](int v){ onDisplacementInput(v / resolution); });
    slider.sigSliderReleased().connect(
        [=](){ baseImpl->onOperationFinished(); });
    
    dial.setSingleStep(0.1 * resolution);
    dial.setProperty("JointDialIndex", index);
    dial.installEventFilter(baseImpl);
    dial.sigValueChanged().connect(
        [=](int v){ onDisplacementInput(v / resolution); });
    
    phaseSpin.sigValueChanged().connect(
        [=](int v){ onPhaseInput(v); });

    phaseSpin.setAlignment(Qt::AlignCenter);
    phaseSpin.setPrefix("T");

    // Prevent the spin from chaning its size during the operation
    phaseSpin.setRange(-9, 9);
    phaseSpin.setMinimumSize(phaseSpin.sizeHint());
}


void JointIndicator::setVisible(bool on)
{
    if(auto label = baseImpl->targetBodyLabel){
        label->setVisible(on);
    }
    spin.setVisible(on);
    slider.setVisible(on && baseImpl->isSliderEnabled);
    dial.setVisible(on && baseImpl->isDialEnabled);
    phaseSpin.setVisible(on && hasPhases);
    idLabel.setVisible(on && baseImpl->isJointIdVisible);
    nameLabel.setVisible(on && baseImpl->isJointNameVisible);
    lowerLimitLabel.setVisible(on && baseImpl->isSliderEnabled);
    upperLimitLabel.setVisible(on && baseImpl->isSliderEnabled);
}


int JointIndicator::attachTo(QGridLayout* grid, int row, int col, bool overlapJointName)
{
    if(overlapJointName){
        grid->addWidget(&nameLabel, row++, col, 1, 6);
        grid->addWidget(&idLabel, row, col++);
    } else {
        grid->addWidget(&idLabel, row, col++);
        grid->addWidget(&nameLabel, row, col++);
    }
    grid->addWidget(&spin, row, col++);
    grid->addWidget(&lowerLimitLabel, row, col++);
    grid->addWidget(&slider, row, col++);
    grid->addWidget(&upperLimitLabel, row, col++);
    grid->addWidget(&dial, row, col++);
    grid->addWidget(&phaseSpin, row, col++);
    
    return row + 1;
}


void JointIndicator::setNextTabOrderIndicator(JointIndicator* next)
{
    QWidget::setTabOrder(&spin, &next->spin);
    QWidget::setTabOrder(&slider, &next->slider);
    QWidget::setTabOrder(&dial, &next->dial);
    QWidget::setTabOrder(&phaseSpin, &next->phaseSpin);
}


bool JointIndicator::setRangeLabelValue(QLabel& label, double value, bool isInfinite, int precision)
{
    int digits = static_cast<int>(log10(fabs(value))) + 1;
    if(digits > 5){
        label.setText("*");
        return false;
    } else {
        label.setText(QString::number(value, 'f', precision));
    }
    return !isInfinite;
}


void JointIndicator::initialize(Link* joint)
{
    this->joint = joint;

    int id = joint->jointId();
    if(id >= 0){
        idLabel.setText(QString("%1:").arg(joint->jointId()));
    } else {
        idLabel.setText("");
    }
    
    nameLabel.setText(joint->jointName().c_str());
    
    spin.show();
    
    unitConversionRatio = 1.0;
    double lower = joint->q_lower();
    bool isLowerInfinite = (joint->q_lower() == -std::numeric_limits<double>::max());
    double upper = joint->q_upper();
    bool isUpperInfinite = (joint->q_upper() == std::numeric_limits<double>::max());
    
    slider.blockSignals(true);
    spin.blockSignals(true);
    dial.blockSignals(true);
    phaseSpin.blockSignals(true);
    
    if(joint->isRevoluteJoint()){
        bool isValidRange = true;
        if(baseImpl->angleUnit == DisplayValueFormat::Degree){
            unitConversionRatio = 180.0 / PI;
            lower *= unitConversionRatio;
            upper *= unitConversionRatio;

            if(baseImpl->isRangeLimitMode){
                if(lower < -360.0){
                    lower = -360.0;
                }
                if(upper > 360.0){
                    upper = 360.0;
                }
            }
            if(!setRangeLabelValue(lowerLimitLabel, lower, isLowerInfinite, 0)){
                lower = -360.0;
                isValidRange = false;
            }
            if(!setRangeLabelValue(upperLimitLabel, upper, isUpperInfinite, 0)){
                upper = 360.0;
                isValidRange = false;
            }
        } else {
            if(baseImpl->isRangeLimitMode){
                if(lower < -2.0 * M_PI){
                    lower = -2.0 * M_PI;
                }
                if(upper > 2.0 * M_PI){
                    upper = 2.0 * M_PI;
                }
            }
            if(!setRangeLabelValue(lowerLimitLabel, lower, isLowerInfinite, baseImpl->angleDecimals)){
                lower = -2.0 * M_PI;
                isValidRange = false;
            }
            if(!setRangeLabelValue(upperLimitLabel, upper, isUpperInfinite, baseImpl->angleDecimals)){
                upper = 2.0 * M_PI;
                isValidRange = false;
            }
        }
        slider.setRange(lower * resolution, upper * resolution);
        slider.setEnabled(true);
        
        spin.setDecimals(baseImpl->angleDecimals);
        spin.setRange(-baseImpl->defaultMaxAngle, baseImpl->defaultMaxAngle);
        spin.setSingleStep(baseImpl->angleStep);
        spin.setEnabled(true);
        
        if(!isValidRange){
            dial.setWrapping(true);
            dial.setNotchesVisible(false);
        } else {
            dial.setWrapping(false);
            dial.setNotchesVisible(true);
        }
        dial.setRange(lower * resolution, upper * resolution);
        dial.setEnabled(true);
        
    } else if(joint->isPrismaticJoint()){
        if(baseImpl->lengthUnit == DisplayValueFormat::Millimeter){
            unitConversionRatio = 1000.0;
            lower *= unitConversionRatio;
            upper *= unitConversionRatio;
            if(!setRangeLabelValue(lowerLimitLabel, lower, isLowerInfinite, baseImpl->lengthDecimals)){
                lower = -1000.0;
            }
            if(!setRangeLabelValue(upperLimitLabel, upper, isUpperInfinite, baseImpl->lengthDecimals)){
                upper = 1000.0;
            }
        } else {
            if(!setRangeLabelValue(lowerLimitLabel, lower, isLowerInfinite, baseImpl->lengthDecimals)){
                lower = -1.0;
            }
            if(!setRangeLabelValue(upperLimitLabel, upper, isUpperInfinite, baseImpl->lengthDecimals)){
                upper = 1.0;
            }
        }
        slider.setRange(lower * resolution, upper * resolution);
        slider.setEnabled(true);
        
        spin.setDecimals(baseImpl->lengthDecimals);
        spin.setRange(-baseImpl->defaultMaxLength, baseImpl->defaultMaxLength);
        spin.setSingleStep(baseImpl->lengthStep);
        spin.setEnabled(true);
        
    } else {
        slider.setRange(0, 0);
        setRangeLabelValue(lowerLimitLabel, 0.0, false, 0);
        setRangeLabelValue(upperLimitLabel, 0.0, false, 0);
        slider.setEnabled(false);
        spin.setDecimals(0);
        spin.setRange(0.0, 0.0);
        spin.setEnabled(false);
    }
    
    hasPhases = false;
    if(baseImpl->isPhaseEnabled){
        minPhase = 0;
        maxPhase = 0;
        double q0 = joint->q_initial();
        if(!isUpperInfinite && (joint->q_upper() - q0) > M_PI){
            maxPhase = 1 + trunc((joint->q_upper() - q0 - M_PI) / (2.0 * M_PI));
            hasPhases = true;
        }
        if(!isLowerInfinite && (joint->q_lower() - q0) < -M_PI){
            minPhase = -1 + trunc((joint->q_lower() - q0 + M_PI) / (2.0 * M_PI));
            hasPhases = true;
        }
        phaseSpin.setRange(minPhase, maxPhase);
    }
    
    spin.blockSignals(false);
    slider.blockSignals(false);
    dial.blockSignals(false);
    phaseSpin.blockSignals(false);

    updateDisplacement(true);
}


void JointIndicator::updateDisplacement(bool forceUpdate)
{
    double q = joint->q();
    double v = unitConversionRatio * q;
    
    if(forceUpdate || v != spin.value()){
        
        if(q > joint->q_upper() || q < joint->q_lower()){
            spin.setStyleSheet(warningStyle);
            slider.setStyleSheet(handleWarningStyle);
            if(q > joint->q_upper()){
                upperLimitLabel.setStyleSheet(warningStyle);
            } else {
                upperLimitLabel.setStyleSheet(normalStyle);
            }
            if(q < joint->q_lower()){
                lowerLimitLabel.setStyleSheet(warningStyle);
            } else {
                lowerLimitLabel.setStyleSheet(normalStyle);
            }
            isWarningState = true;
            
        } else if(isWarningState){
            spin.setStyleSheet(normalStyle);
            slider.setStyleSheet(handleNormalStyle);
            lowerLimitLabel.setStyleSheet(normalStyle);
            upperLimitLabel.setStyleSheet(normalStyle);
            isWarningState = false;
        }
        
        spin.blockSignals(true);
        if(v > spin.maximum()){
            spin.setRange(spin.minimum(), v);
        } else if(v < spin.minimum()){
            spin.setRange(v, spin.maximum());
        }
        spin.setValue(v);
        spin.blockSignals(false);
        
        if(slider.isVisible()){
            slider.blockSignals(true);
            slider.setValue(v * resolution);
            slider.blockSignals(false);
        }
        if(dial.isVisible()){
            dial.blockSignals(true);
            dial.setValue(v * resolution);
            dial.blockSignals(false);
        }
        if(phaseSpin.isVisible()){
            phaseSpin.blockSignals(true);
            int phase = getCurrentPhase();
            int currentMinPhase = minPhase;
            int currentMaxPhase = maxPhase;
            if(joint->q() - 2.0 * M_PI < joint->q_lower() && phase > minPhase){
                ++currentMinPhase;
            }
            if(joint->q() + 2.0 * M_PI > joint->q_upper() && phase < maxPhase){
                --currentMaxPhase;
            }
            phaseSpin.setRange(currentMinPhase, currentMaxPhase);
            phaseSpin.setValue(phase);
            phaseSpin.blockSignals(false);
        }
    }
}


int JointIndicator::getCurrentPhase()
{        
    int phase = 0;
    double q0 = joint->q_initial();
    if((joint->q() - q0) > M_PI){
        phase = 1 + trunc((joint->q() - q0 - M_PI) / (2.0 * M_PI));
    } else if((joint->q() - q0) < -M_PI){
        phase = -1 + trunc((joint->q() - q0 + M_PI) / (2.0 * M_PI));
    }
    return phase;
}


void JointIndicator::onDisplacementInput(double value)
{
    joint->q() = value / unitConversionRatio;
    updateDisplacement(true);
    baseImpl->notifyJointDisplacementInput();
}


void JointIndicator::onPhaseInput(int phase)
{
    int currentPhase = getCurrentPhase();
    joint->q() += (phase - currentPhase) * 2.0 * M_PI;
    updateDisplacement(true);
    baseImpl->notifyJointDisplacementInput();
}


void JointIndicator::removeWidgetsFrom(QGridLayout* grid)
{
    grid->removeWidget(&idLabel);
    grid->removeWidget(&nameLabel);
    grid->removeWidget(&spin);
    grid->removeWidget(&lowerLimitLabel);
    grid->removeWidget(&slider);
    grid->removeWidget(&upperLimitLabel);
    grid->removeWidget(&dial);
    grid->removeWidget(&phaseSpin);
}

}


bool JointDisplacementWidgetSet::Impl::eventFilter(QObject* object, QEvent* event)
{
    Slider* slider = dynamic_cast<Slider*>(object);
    if(slider && (event->type() == QEvent::KeyPress)){
        return onSliderKeyPressEvent(slider, static_cast<QKeyEvent*>(event));
    }

    Dial* dial = dynamic_cast<Dial*>(object);
    if(dial && (event->type() == QEvent::KeyPress)){
        return onDialKeyPressEvent(dial, static_cast<QKeyEvent*>(event));
    }

    return QObject::eventFilter(object, event);
}


bool JointDisplacementWidgetSet::Impl::onSliderKeyPressEvent(Slider* slider, QKeyEvent* event)
{
    int index = slider->property("JointSliderIndex").toInt();
    bool doContinue = false;
    switch(event->key()){
    case Qt::Key_Up:
        focusSlider(index - 1);
        break;
    case Qt::Key_Down:
        focusSlider(index + 1);
        break;
    default:
        doContinue = true;
        break;
    }
    return !doContinue;
}


bool JointDisplacementWidgetSet::Impl::onDialKeyPressEvent(Dial* dial, QKeyEvent* event)
{
    int index = dial->property("JointDialIndex").toInt();
    bool doContinue = false;
    switch(event->key()){
    case Qt::Key_Up:
        focusDial(index - 1);
        break;
    case Qt::Key_Down:
        focusDial(index + 1);
        break;
    default:
        doContinue = true;
        break;
    }
    return !doContinue;
}


SignalProxy<void(QWidget* widget)> JointDisplacementWidgetSet::sigJointWidgetFocused()
{
    return impl->sigJointWidgetFocused;
}


void JointDisplacementWidgetSet::Impl::focusSlider(int index)
{
    if(index >= 0 && index < static_cast<int>(jointIndicators.size())){
        Slider& slider = jointIndicators[index]->slider;
        slider.setFocus(Qt::OtherFocusReason);
        sigJointWidgetFocused(&slider);
    }
}


void JointDisplacementWidgetSet::Impl::focusDial(int index)
{
    if(index >= 0 && index < static_cast<int>(jointIndicators.size())){
        Dial& dial = jointIndicators[index]->dial;
        dial.setFocus(Qt::OtherFocusReason);
        sigJointWidgetFocused(&dial);
    }
}


void JointDisplacementWidgetSet::Impl::onOperationFinished()
{
    currentBodyItem->notifyKinematicStateUpdate(false);
}


void JointDisplacementWidgetSet::Impl::notifyJointDisplacementInput()
{
    kinematicStateChangeConnection.block();
    currentBodyItem->notifyKinematicStateChange(true);
    kinematicStateChangeConnection.unblock();
}


void JointDisplacementWidgetSet::Impl::updateJointDisplacements()
{
    for(size_t i=0; i < activeJointLinkIndices.size(); ++i){
        jointIndicators[i]->updateDisplacement(false);
    }
}


bool JointDisplacementWidgetSet::storeState(Archive* archive)
{
    return impl->storeState(archive);
}


bool JointDisplacementWidgetSet::Impl::storeState(Archive* archive)
{
    archive->write("show_selected_joints", isSelectedJointsOnlyMode);
    archive->write("show_joint_ids", isJointIdVisible);
    archive->write("show_joint_names", isJointNameVisible);
    archive->write("overlap_joint_names", isOverlapJointNameMode);
    archive->write("show_sliders", isSliderEnabled);
    archive->write("show_dials", isDialEnabled);
    archive->write("show_phases", isPhaseEnabled);
    archive->write("limit_ranges", isRangeLimitMode);
    return true;
}


bool JointDisplacementWidgetSet::restoreState(const Archive* archive)
{
    return impl->restoreState(archive);
}


bool JointDisplacementWidgetSet::Impl::restoreState(const Archive* archive)
{
    archive->read("show_selected_joints", isSelectedJointsOnlyMode);
    archive->read("show_joint_ids", isJointIdVisible);
    archive->read("show_joint_names", isJointNameVisible);
    archive->read("overlap_joint_names", isOverlapJointNameMode);
    archive->read("show_sliders", isSliderEnabled);
    archive->read("show_dials", isDialEnabled);
    archive->read("show_phases", isPhaseEnabled);
    archive->read("limit_ranges", isRangeLimitMode);
    return true;
}
