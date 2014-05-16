#ifndef SURFACE_GAP_ONBOARDING_RECEIVEMACHINE_HH
# define SURFACE_GAP_ONBOARDING_RECEIVEMACHINE_HH

# include <elle/attribute.hh>

# include <surface/gap/ReceiveMachine.hh>
# include <surface/gap/onboarding/fwd.hh>

namespace surface
{
  namespace gap
  {
    namespace onboarding
    {
      class ReceiveMachine:
        public surface::gap::ReceiveMachine
      {
      public:
        ReceiveMachine(Transaction& transaction,
                       uint32_t id,
                       std::shared_ptr<Data> data,
                       std::string const& file_path,
                       reactor::Duration duration = 5_sec);

        float
        progress() const override;

        void
        accept();

        bool
        pause() override;

        void
        interrupt() override;

        // Overload because it talks to meta.
        void
        _accept() override;

        // Overload because it talks to meta.
        void
        _finalize(infinit::oracles::Transaction::Status) override;

        ELLE_ATTRIBUTE(std::string, file_path);
      };
    }
  }
}

#endif
