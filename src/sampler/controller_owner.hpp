#ifndef CONTROLLER_OWNER_HPP
#define CONTROLLER_OWNER_HPP

namespace Sampler {

template <typename Controller> class ControllerOwner {
  public:
    explicit ControllerOwner(Controller* value) : value_(value) {}
    ~ControllerOwner() { reset(); }
    Controller* operator->() const { return value_; }
    Controller* get() const { return value_; }
    void reset() {
        if (!value_) return;
        Controller* value = value_;
        value_ = nullptr;
        value->Dispose();
    }

  private:
    ControllerOwner(const ControllerOwner&);
    ControllerOwner& operator=(const ControllerOwner&);
    Controller* value_;
};

} // namespace Sampler
#endif
