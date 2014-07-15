#include <surface/gap/LinkTransaction.hh>

namespace surface
{
  namespace gap
  {
    LinkTransaction::LinkTransaction(uint32_t id_,
                                     std::string name_,
                                     double mtime_,
                                     boost::optional<std::string> link_,
                                     uint32_t click_count_,
                                     gap_TransactionStatus status_)
      : id(id_)
      , name(std::move(name_))
      , mtime(mtime_)
      , link()
      , click_count(click_count_)
      , status(status_)
    {
      // FIXME: for now the server always return the share link, as an empty
      // string if not ready. Remove when we remove the old serialization,
      // can handle optionals and not return an empty string.
      if (link_ && !link_.get().empty())
        this->link = std::move(link_);
    }

    LinkTransaction::~LinkTransaction() noexcept(true)
    {}

    Notification::Type LinkTransaction::type = NotificationType_LinkUpdate;
  }
}