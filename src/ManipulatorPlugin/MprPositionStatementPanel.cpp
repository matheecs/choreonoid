#include "MprPositionStatementPanel.h"
#include "MprPositionStatement.h"
#include "MprProgramItemBase.h"
#include "MprControllerItemBase.h"
#include <cnoid/MprPosition>
#include <cnoid/MprPositionList>
#include <cnoid/KinematicBodyItemSet>
#include <cnoid/LinkKinematicsKit>
#include <cnoid/JointPath>
#include <cnoid/DisplayValueFormat>
#include <cnoid/EigenUtil>
#include <cnoid/Buttons>
#include <cnoid/MessageOut>
#include <cnoid/stdx/variant>
#include <QLabel>
#include <QBoxLayout>
#include <QGridLayout>
#include <fmt/format.h>
#include "gettext.h"

using namespace std;
using namespace cnoid;
using fmt::format;

namespace {

const QString normalStyle("font-weight: normal");
const QString errorStyle("font-weight: bold; color: red");

typedef stdx::variant<LinkKinematicsKit*, shared_ptr<JointTraverse>> BodyPart;

class PositionPartPanel : public QWidget
{
public:
    QLabel bodyPartLabel;
    QWidget ikPanel;
    QLabel xyzLabels[3];
    QLabel rpyLabels[3];
    QLabel coordinateFrameLabels[2];
    QLabel configLabel;
    QWidget fkPanel;
    QLabel jointDisplacementLabels[MprFkPosition::MaxNumJoints];
    QLabel errorLabel;

    PositionPartPanel();
    void createIkPanel();
    void createFkPanel();
    void showPositionWidgetsOfType(MprPosition::PositionType positionType);
    bool update(BodyPart bodyPart, MprPosition* position);
    bool updateIkPanel(BodyPart bodyPart, MprIkPosition* position);
    bool updateFkPanel(BodyPart bodyPart, MprFkPosition* position);
};

}

namespace cnoid {

class MprPositionStatementPanel::Impl
{
public:
    MprPositionStatementPanel* self;
    QWidget topPanel;
    QWidget positionPanel;
    QLabel positionNameLabel;
    PushButton moveToButton;
    PushButton touchupButton;
    QVBoxLayout* positionPartPanelVBox;
    vector<PositionPartPanel*> positionPartPanels;
    int numActivePositionPartPanels;
    vector<int> redundantPositionIndices;

    KinematicBodyItemSetPtr currentBodyItemSet;
    LinkKinematicsKitPtr currentKinematicsKit;

    Impl(MprPositionStatementPanel* self);
    ~Impl();
    PositionPartPanel* getOrCreatePositionPartPanel(int index);
    void updatePositionPanel();
    void updatePositionPartPanel(MprPosition* position, BodyPart bodyPart, int& io_panelIndex);
    void updateMultiPositionPartPanels(MprPosition* position, int& io_panelIndex);
    void updateIkPositionPanel(MprIkPosition* position, LinkKinematicsKit* kinematicsKit);
    void updateFkPositionPanel(MprFkPosition* position);
};

}


MprPositionStatementPanel::MprPositionStatementPanel()
{
    impl = new Impl(this);
}


MprPositionStatementPanel::Impl::Impl(MprPositionStatementPanel* self)
    : self(self)
{
    auto topHBox = new QHBoxLayout;
    self->setLayout(topHBox);

    auto topVBox = new QVBoxLayout;
    topVBox->setContentsMargins(0, 0, 0, 0);
    topHBox->addLayout(topVBox);
    topHBox->addStretch();

    topVBox->addWidget(&topPanel);

    auto positionPanelVBox = new QVBoxLayout;
    positionPanel.setLayout(positionPanelVBox);

    auto hbox = new QHBoxLayout;
    hbox->addWidget(new QLabel(_("Position :")));
    hbox->addWidget(&positionNameLabel);

    hbox->addSpacing(10);

    moveToButton.setText(_("Move to"));
    moveToButton.sigClicked().connect(
        [self](){
            self->currentProgramItem()->moveTo(
                self->currentStatement<MprPositionStatement>(), MessageOut::interactive());
        });
    hbox->addWidget(&moveToButton);

    touchupButton.setText(_("Touch-up"));
    touchupButton.sigClicked().connect(
        [self](){
            self->currentProgramItem()->touchupPosition(
                self->currentStatement<MprPositionStatement>(), MessageOut::interactive());
        });
    hbox->addWidget(&touchupButton);
    
    hbox->addStretch();
    positionPanelVBox->addLayout(hbox);

    positionPartPanelVBox = new QVBoxLayout;
    positionPanelVBox->addLayout(positionPartPanelVBox);

    topVBox->addWidget(&positionPanel);
    topVBox->addStretch();

    numActivePositionPartPanels = 0;
}


MprPositionStatementPanel::~MprPositionStatementPanel()
{
    delete impl;
}


MprPositionStatementPanel::Impl::~Impl()
{
    for(auto& panel : positionPartPanels){
        delete panel;
    }
}


QWidget* MprPositionStatementPanel::topPanel()
{
    return &impl->topPanel;
}


QWidget* MprPositionStatementPanel::positionPanel()
{
    return &impl->positionPanel;
}


void MprPositionStatementPanel::setEditable(bool on)
{
    impl->touchupButton.setEnabled(on);
}


void MprPositionStatementPanel::onActivated()
{
    impl->currentBodyItemSet.reset();
    impl->currentKinematicsKit.reset();

    auto programItem = currentProgramItem();
    if(auto controllerItem = programItem->findOwnerItem<MprControllerItemBase>()){
        impl->currentBodyItemSet = controllerItem->kinematicBodyItemSet();
    } else {
        impl->currentKinematicsKit = programItem->kinematicsKit();
    }
}


void MprPositionStatementPanel::onStatementUpdated()
{
    impl->updatePositionPanel();
}


void MprPositionStatementPanel::onDeactivated()
{
    impl->currentBodyItemSet.reset();
    impl->currentKinematicsKit.reset();
}


PositionPartPanel* MprPositionStatementPanel::Impl::getOrCreatePositionPartPanel(int index)
{
    if(index >= static_cast<int>(positionPartPanels.size())){
        positionPartPanels.resize(index + 1);
    }
    auto& panel = positionPartPanels[index];
    if(!panel){
        panel = new PositionPartPanel;
        positionPartPanelVBox->addWidget(panel);
    }
    return panel;
}


void MprPositionStatementPanel::updatePositionPanel()
{
    impl->updatePositionPanel();
}


void MprPositionStatementPanel::Impl::updatePositionPanel()
{
    auto statement = self->currentStatement<MprPositionStatement>();
    auto position = statement->position();

    if(!position){
        positionNameLabel.setText(
            QString(_("%1 ( Not found )")).arg(statement->positionLabel().c_str()));
        positionNameLabel.setStyleSheet(errorStyle);

    } else {
        positionNameLabel.setText(statement->positionLabel().c_str());
        positionNameLabel.setStyleSheet(normalStyle);
    }

    int panelIndex = 0;
    numActivePositionPartPanels = 0;
    
    if(!currentBodyItemSet){
        updatePositionPartPanel(position, currentKinematicsKit, panelIndex);
    } else {
        updateMultiPositionPartPanels(position, panelIndex);
    }

    panelIndex = 0;
    bool showBodyPartLabels = numActivePositionPartPanels >= 2;
    while(panelIndex < numActivePositionPartPanels){
        auto panel = positionPartPanels[panelIndex];
        panel->bodyPartLabel.setVisible(showBodyPartLabels);
        panel->show();
        ++panelIndex;
    }
    while(panelIndex < static_cast<int>(positionPartPanels.size())){
        positionPartPanels[panelIndex]->hide();
        ++panelIndex;
    }
}


void MprPositionStatementPanel::Impl::updatePositionPartPanel(MprPosition* position, BodyPart bodyPart, int& io_panelIndex)
{
    if(position->isComposite()){
        auto composite = position->compositePosition();
        position = composite->mainPosition();
        redundantPositionIndices = composite->nonMainPositionIndices();
    }

    if(position){
        auto partPanel = getOrCreatePositionPartPanel(io_panelIndex++);
        partPanel->update(bodyPart, position);
        ++numActivePositionPartPanels;
    }
}


static BodyPart convertBodyPart(KinematicBodyItemSet::BodyItemPart* bodyItemPart)
{
    BodyPart bodyPart;
    if(bodyItemPart->isLinkKinematicsKit()){
        bodyPart = bodyItemPart->linkKinematicsKit();
    } else if(bodyItemPart->isJointTraverse()){
        bodyPart = bodyItemPart->jointTraverse();
    }
    return bodyPart;
}


void MprPositionStatementPanel::Impl::updateMultiPositionPartPanels(MprPosition* position, int& io_panelIndex)
{
    if(!position->isComposite()){
        if(auto bodyItemPart = currentBodyItemSet->mainBodyItemPart()){
            updatePositionPartPanel(position, convertBodyPart(bodyItemPart), io_panelIndex);
        }
    } else {
        auto composite = position->compositePosition();
        for(auto& partIndex : composite->findMatchedPositionIndices(currentBodyItemSet)){
            updatePositionPartPanel(
                composite->position(partIndex),
                convertBodyPart(currentBodyItemSet->bodyItemPart(partIndex)),
                io_panelIndex);
        }
        redundantPositionIndices = composite->findUnMatchedPositionIndices(currentBodyItemSet);
    }
}


void MprPositionStatementPanel::updateCoordinateFrameLabel
(QLabel& label, const GeneralId& id, CoordinateFrame* frame, CoordinateFrameList* frames)
{
    bool updated = false;
    if(frames){
        if(frame){
            auto& note = frame->note();
            if(id.isInt() && !note.empty()){
                label.setText(QString("%1 ( %2 )").arg(id.toInt()).arg(note.c_str()));
            } else {
                label.setText(id.label().c_str());
            }
            label.setStyleSheet(normalStyle);
        } else { // Not found
            label.setText(QString("%1 ( Not found )").arg(id.label().c_str()));
            label.setStyleSheet(errorStyle);
        }
        updated = true;
    }
    if(!updated){
        label.setText("---");
        label.setStyleSheet(normalStyle);
    }
}


PositionPartPanel::PositionPartPanel()
{
    auto vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->addWidget(&bodyPartLabel);
    createIkPanel();
    vbox->addWidget(&ikPanel);
    createFkPanel();
    vbox->addWidget(&fkPanel);
    vbox->addWidget(&errorLabel);
    vbox->addStretch();
}


void PositionPartPanel::createIkPanel()
{
    auto vbox = new QVBoxLayout(&ikPanel);
    vbox->setContentsMargins(0, 0, 0, 0);
    
    auto grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);

    static const char* xyzCaptions[] = { "X:", "Y:", "Z:" };
    static const char* rpyCaptions[] = { "R:", "P:", "Y:" };

    for(int i=0; i < 3; ++i){
        grid->addWidget(new QLabel(xyzCaptions[i]), 0, i * 2, Qt::AlignCenter);
        auto xyzLabel = &xyzLabels[i];
        grid->addWidget(xyzLabel, 0, i * 2 + 1, Qt::AlignCenter);
        grid->addWidget(new QLabel(rpyCaptions[i]), 1, i * 2, Qt::AlignCenter);
        auto rpyLabel = &rpyLabels[i];
        grid->addWidget(rpyLabel, 1, i * 2 + 1, Qt::AlignCenter);
        grid->setColumnStretch(i * 2, 0);
        grid->setColumnStretch(i * 2 + 1, 1);
    }
    vbox->addLayout(grid);

    grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    int row = 0;
    
    grid->addWidget(new QLabel(_("Base")), row, 0);
    grid->addWidget(new QLabel(":"), row, 1);
    grid->addWidget(&coordinateFrameLabels[0], row, 2);
    ++row;
    
    grid->addWidget(new QLabel(_("Tool")), row, 0);
    grid->addWidget(new QLabel(":"), row, 1);
    grid->addWidget(&coordinateFrameLabels[1], row, 2);
    ++row;

    grid->addWidget(new QLabel(_("Config")), row, 0);
    grid->addWidget(new QLabel(":"), row, 1);
    grid->addWidget(&configLabel, row, 2);
    ++row;

    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 0);
    grid->setColumnStretch(2, 1);
    
    vbox->addLayout(grid);
}


void PositionPartPanel::createFkPanel()
{
    auto grid = new QGridLayout(&fkPanel);
    grid->setContentsMargins(0, 0, 0, 0);
    for(int i=0; i < MprPosition::MaxNumJoints; ++i){
        int row = i / 6;
        int column = i % 6;
        grid->addWidget(&jointDisplacementLabels[i], row, column, Qt::AlignCenter);
    }
}


void PositionPartPanel::showPositionWidgetsOfType(MprPosition::PositionType positionType)
{
    ikPanel.setVisible(positionType == MprPosition::IK);
    fkPanel.setVisible(positionType == MprPosition::FK);
}


static string getBodyPartName(const BodyPart& bodyPart)
{
    if(stdx::holds_alternative<LinkKinematicsKit*>(bodyPart)){
        return stdx::get<LinkKinematicsKit*>(bodyPart)->body()->name();
    } else if(stdx::holds_alternative<shared_ptr<JointTraverse>>(bodyPart)){
        if(auto body = stdx::get<shared_ptr<JointTraverse>>(bodyPart)->body()){
            return body->name();
        }
    }
    return std::string();
}


bool PositionPartPanel::update(BodyPart bodyPart, MprPosition* position)
{
    bool updated = false;

    bodyPartLabel.setText(getBodyPartName(bodyPart).c_str());

    if(position->isIK()){
        showPositionWidgetsOfType(MprPosition::IK);
        updated = updateIkPanel(bodyPart, position->ikPosition());

    } else if(position->isFK()){
        showPositionWidgetsOfType(MprPosition::FK);
        updated = updateFkPanel(bodyPart, position->fkPosition());
    }

    if(updated){
        errorLabel.hide();
    } else {
        errorLabel.setText(_("Invalid"));
        errorLabel.setStyleSheet("font-weight: bold; color: red");
        errorLabel.show();
    }

    return updated;
}


bool PositionPartPanel::updateIkPanel(BodyPart bodyPart, MprIkPosition* position)
{
    if(!stdx::holds_alternative<LinkKinematicsKit*>(bodyPart)){
        return false;
    }

    auto kinematicsKit = stdx::get<LinkKinematicsKit*>(bodyPart);
        
    auto xyz = position->position().translation();
    if(DisplayValueFormat::instance()->isMillimeter()){
        for(int i=0; i < 3; ++i){
            xyzLabels[i].setText(QString::number(xyz[i] * 1000.0, 'f', 3));
        }
    } else {
        for(int i=0; i < 3; ++i){
            xyzLabels[i].setText(QString::number(xyz[i], 'f', 4));
        }
    }
    auto rpy = position->rpy();
    for(int i=0; i < 3; ++i){
        rpyLabels[i].setText(QString::number(degree(rpy[i]), 'f', 1));
    }

    MprPositionStatementPanel::updateCoordinateFrameLabel(
        coordinateFrameLabels[0], position->baseFrameId(),
        position->findBaseFrame(kinematicsKit->baseFrames()),
        kinematicsKit->baseFrames());

    MprPositionStatementPanel::updateCoordinateFrameLabel(
        coordinateFrameLabels[1], position->offsetFrameId(),
        position->findOffsetFrame(kinematicsKit->offsetFrames()),
        kinematicsKit->offsetFrames());

    int configIndex = position->configuration();
    if(kinematicsKit){
        string configName = kinematicsKit->configurationLabel(configIndex);
        configLabel.setText(format("{0:X} ( {1} )", configIndex, configName).c_str());
    } else {
        configLabel.setText(QString::number(configIndex));
    }

    return true;
}


bool PositionPartPanel::updateFkPanel(BodyPart bodyPart, MprFkPosition* position)
{
    vector<string> jointNames;
    if(stdx::holds_alternative<LinkKinematicsKit*>(bodyPart)){
        if(auto jointPath = stdx::get<LinkKinematicsKit*>(bodyPart)->jointPath()){
            jointNames.reserve(jointPath->size());
            for(auto& joint : *jointPath){
                jointNames.push_back(joint->jointName());
            }
        }
    } else if(stdx::holds_alternative<shared_ptr<JointTraverse>>(bodyPart)){
        auto joints = stdx::get<shared_ptr<JointTraverse>>(bodyPart);
        jointNames.reserve(joints->numJoints());
        for(auto& joint : *joints){
            jointNames.push_back(joint->jointName());
        }
    }

    if(jointNames.empty()){
        return false;
    }
    
    const int n = position->numJoints();
    int i = 0;
    while(i < n){
        auto& label = jointDisplacementLabels[i];
        label.setVisible(true);
        double q = position->jointDisplacement(i);
        if(position->checkIfRevoluteJoint(i)){
            label.setText(QString::number(degree(q), 'f', 1));
        } else {
            label.setText(QString::number(q, 'f', 3));
        }
        ++i;
    }
    while(i < MprPosition::MaxNumJoints){
        jointDisplacementLabels[i++].setVisible(false);
    }

    return true;
}
