#ifndef CNOID_MANIPULATOR_PLUGIN_MPR_POSITION_H
#define CNOID_MANIPULATOR_PLUGIN_MPR_POSITION_H

#include <cnoid/CoordinateFrameList>
#include <cnoid/ClonableReferenced>
#include <cnoid/GeneralId>
#include <cnoid/EigenTypes>
#include <cnoid/Signal>
#include <string>
#include <array>
#include <bitset>
#include "exportdecl.h"

namespace cnoid {

class Body;
class LinkKinematicsKit;
class LinkKinematicsKitSet;
class MprIkPosition;
class MprFkPosition;
class MprCompositePosition;
class MprPositionList;
class Mapping;
class MessageOut;

class CNOID_EXPORT MprPosition : public ClonableReferenced
{
public:
    static constexpr int MaxNumJoints = 8;
    
    enum PositionType { IK, FK, Composite };

    MprPosition& operator=(const MprPosition& rhs) = delete;

    MprPosition* clone() const {
        return static_cast<MprPosition*>(doClone(nullptr));
    }
    
    const GeneralId& id() const { return id_; }

    //! \note This function only works when the position is not belonging to any position list.
    void setId(const GeneralId& id);

    int positionType() const { return positionType_; }
    bool isIK() const { return (positionType_ == IK); };
    bool isFK() const { return (positionType_ == FK); };
    bool isComposite() const { return (positionType_ == Composite); };

    MprIkPosition* ikPosition();
    MprFkPosition* fkPosition();
    MprCompositePosition* compositePosition();

    MprPositionList* ownerPositionList();

    virtual bool fetch(LinkKinematicsKit* kinematicsKit, MessageOut* mout = nullptr) = 0;
    virtual bool apply(LinkKinematicsKit* kinematicsKit) const = 0;

    virtual bool fetch(LinkKinematicsKitSet* kinematicsKitSet, MessageOut* mout = nullptr);
    virtual bool apply(LinkKinematicsKitSet* kinematicsKitSet) const;

    const std::string& note() const { return note_; }
    void setNote(const std::string& note) { note_ = note; }

    virtual bool read(const Mapping& archive);
    virtual bool write(Mapping& archive) const;

    enum UpdateFlag {
        IdUpdate = 1 << 0,
        NoteUpdate = 1 << 1,
        PositionUpdate = 1 << 2,
        ObjectReplaced = 1 << 3
    };
    SignalProxy<void(int flags)> sigUpdated() { return sigUpdated_; }
    void notifyUpdate(int flags);

protected:
    MprPosition(PositionType type);
    MprPosition(PositionType type, const GeneralId& id);
    MprPosition(const MprPosition& org);
    
private:
    PositionType positionType_;
    GeneralId id_;
    std::string note_;
    weak_ref_ptr<MprPositionList> ownerPositionList_;
    Signal<void(int flags)> sigUpdated_;

    friend class MprPositionList;
};

typedef ref_ptr<MprPosition> MprPositionPtr;


class CNOID_EXPORT MprIkPosition : public MprPosition
{
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

    MprIkPosition();
    MprIkPosition(const GeneralId& id);
    MprIkPosition(const MprIkPosition& org);
    MprIkPosition& operator=(const MprIkPosition& rhs) = delete;

    const Isometry3& position() const { return T; }
    void setPosition(const Isometry3& T) { this->T = T; }
    Vector3 rpy() const;
    void setRpy(const Vector3& rpy);
    const Vector3 referenceRpy() const { return referenceRpy_; }
    void setReferenceRpy(const Vector3& rpy);
    void resetReferenceRpy();

    void setBaseFrameId(const GeneralId& id){ baseFrameId_ = id; }
    void setOffsetFrameId(const GeneralId& id){ offsetFrameId_ = id; }
    const GeneralId& baseFrameId() const { return baseFrameId_; }
    const GeneralId& offsetFrameId() const { return offsetFrameId_; }

    enum FrameType { BaseFrame = 0, OffsetFrame = 1 };
    const GeneralId& frameId(int frameType) const {
        return (frameType == BaseFrame) ? baseFrameId_ : offsetFrameId_;
    }

    CoordinateFrame* findBaseFrame(CoordinateFrameList* baseFrames){
        return baseFrames->findFrame(baseFrameId_);
    }
    CoordinateFrame* findOffsetFrame(CoordinateFrameList* offsetFrames){
        return offsetFrames->findFrame(offsetFrameId_);
    }
    CoordinateFrame* findFrame(CoordinateFrameList* frames, int frameType){
        return (frameType == BaseFrame) ? findBaseFrame(frames) : findOffsetFrame(frames);
    }
    
    int configuration() const { return configuration_; }

    /**
       \note The configuration is usually determined by the manipulator pose when the fetch
       function is executed, so this function should not used except in special cases.
       It may be better to remove this function if the function is not necessary.
    */
    void setConfiguration(int conf) { configuration_ = conf; }

    //! \note This function always specifies BodyFrame as the base frame type.
    virtual bool fetch(LinkKinematicsKit* kinematicsKit, MessageOut* mout = nullptr) override;
    virtual bool apply(LinkKinematicsKit* kinematicsKit) const override;
    virtual bool read(const Mapping& archive) override;
    virtual bool write(Mapping& archive) const override;

protected:
    virtual Referenced* doClone(CloneMap* cloneMap) const override;
    
private:
    Isometry3 T;
    Vector3 referenceRpy_;
    GeneralId baseFrameId_;
    GeneralId offsetFrameId_;
    int configuration_;
    std::array<int, MaxNumJoints> phase_;
};

typedef ref_ptr<MprIkPosition> MprIkPositionPtr;


class CNOID_EXPORT MprFkPosition : public MprPosition
{
    typedef std::array<double, MaxNumJoints> JointDisplacementArray;
    
public:
    MprFkPosition();
    MprFkPosition(const GeneralId& id);
    MprFkPosition(const MprFkPosition& org);
    MprFkPosition& operator=(const MprFkPosition& rhs) = delete;

    virtual bool fetch(LinkKinematicsKit* kinematicsKit, MessageOut* mout = nullptr) override;
    virtual bool apply(LinkKinematicsKit* kinematicsKit) const override;
    virtual bool read(const Mapping& archive) override;
    virtual bool write(Mapping& archive) const override;

    int numJoints() const { return numJoints_; }

    typedef JointDisplacementArray::iterator iterator;
    typedef JointDisplacementArray::const_iterator const_iterator;

    iterator begin() { return jointDisplacements_.begin(); }
    const_iterator begin() const { return jointDisplacements_.cbegin(); }
    iterator end() { return jointDisplacements_.begin() + numJoints_; }
    const_iterator end() const { return jointDisplacements_.cbegin() + numJoints_; }

    double& jointDisplacement(int index) { return jointDisplacements_[index]; }
    double jointDisplacement(int index) const { return jointDisplacements_[index]; }
    double& q(int index) { return jointDisplacements_[index]; }
    double q(int index) const { return jointDisplacements_[index]; }

    bool checkIfPrismaticJoint(int index) const { return prismaticJointFlags_[index]; }
    bool checkIfRevoluteJoint(int index) const { return !prismaticJointFlags_[index]; }
        
protected:
    virtual Referenced* doClone(CloneMap* cloneMap) const override;
    
private:
    JointDisplacementArray jointDisplacements_;
    std::bitset<MaxNumJoints> prismaticJointFlags_;
    int numJoints_;
};

typedef ref_ptr<MprFkPosition> MprFkPositionPtr;


class CNOID_EXPORT MprCompositePosition : public MprPosition
{
public:
    MprCompositePosition();
    MprCompositePosition(const GeneralId& id);
    MprCompositePosition(const MprCompositePosition& org, CloneMap* cloneMap);
    MprCompositePosition& operator=(const MprCompositePosition& rhs) = delete;

    void clearPositions();
    void setNumPositions(int n);
    void setPosition(int index, MprPosition* position);
    int numPositions() const { return positions_.size(); }
    MprPosition* position(int index) { return positions_[index]; }
    const MprPosition* position(int index) const { return positions_[index]; }
    int mainPositionIndex() const { return mainPositionIndex_; }
    void setMainPositionIndex(int index) { mainPositionIndex_ = index; }
    MprPosition* mainPosition() {
        return mainPositionIndex_ >= 0 ? positions_[mainPositionIndex_] : nullptr;
    }
    const MprPosition* mainPosition() const {
        return const_cast<MprCompositePosition*>(this)->mainPosition();
    }
    
    virtual bool fetch(LinkKinematicsKitSet* kinematicsKitSet, MessageOut* mout = nullptr) override;
    virtual bool apply(LinkKinematicsKitSet* kinematicsKitSet) const override;

    virtual bool fetch(LinkKinematicsKit* kinematicsKit, MessageOut* mout = nullptr) override;
    virtual bool apply(LinkKinematicsKit* kinematicsKit) const override;
    
    virtual bool read(const Mapping& archive) override;
    virtual bool write(Mapping& archive) const override;

protected:
    virtual Referenced* doClone(CloneMap* cloneMap) const override;

private:
    std::vector<MprPositionPtr> positions_;
    int mainPositionIndex_;
};

}

#endif
