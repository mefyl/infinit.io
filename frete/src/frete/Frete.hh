#ifndef FRETE_FRETE_HH
# define FRETE_FRETE_HH

# include <ios>

# include <boost/filesystem.hpp>

# include <elle/serialize/BinaryArchive.hh>
# include <elle/Buffer.hh>

# include <protocol/ChanneledStream.hh>
# include <protocol/Serializer.hh>
# include <protocol/RPC.hh>

namespace frete
{
  class Frete
  {
  /*------.
  | Types |
  `------*/
  public:
    typedef uint32_t FileID;
    typedef uint64_t Offset;
    typedef uint64_t Size;
    typedef Frete Self;

  /*-------------.
  | Construction |
  `-------------*/
  public:
    Frete(infinit::protocol::ChanneledStream& channels);
    ~Frete();

  /*-------------.
  | Run           |
  `-------------*/
  public:
    void
    run();

  /*--------------------.
  | Local configuration |
  `--------------------*/
  public:
    void
    add(boost::filesystem::path const& path);
    void
    add(boost::filesystem::path const& root,
        boost::filesystem::path const& path);
    void
    get(boost::filesystem::path const& output);

    /*-------------.
    | Remote calls |
    `-------------*/
  public:
    /// The number of file in the remote filesystem.
    uint64_t
    count();
    /// The size of a remote file.
    uint64_t
    file_size(FileID f);
    /// The path of the \a f file.
    std::string
    path(FileID f);
    /// A chunk of a remote file.
    elle::Buffer
    read(FileID f, Offset start, Size size);
    /// Tell the peer the current progress.

  /*-----.
  | RPCs |
  `-----*/
  private:
    boost::filesystem::path
    _local_path(FileID file_id);
    uint64_t
    _count();
    uint64_t
    _file_size(FileID f);
    std::string
    _path(FileID f);
    elle::Buffer
    _read(FileID f, Offset start, Size size);
    typedef std::pair<boost::filesystem::path, boost::filesystem::path> Path;
    typedef std::vector<Path> Paths;
    ELLE_ATTRIBUTE(Paths, paths);
    typedef infinit::protocol::RPC<
      elle::serialize::InputBinaryArchive,
      elle::serialize::OutputBinaryArchive> RPC;
    RPC _rpc;
    RPC::RemoteProcedure<uint64_t> _rpc_count;
    RPC::RemoteProcedure<uint64_t, FileID> _rpc_size_file;
    RPC::RemoteProcedure<std::string,
                         FileID> _rpc_path;
    RPC::RemoteProcedure<elle::Buffer,
                         FileID,
                         Offset,
                         Size> _rpc_read;

    ELLE_ATTRIBUTE_R(uint64_t, total_size);
    ELLE_ATTRIBUTE(uint64_t, progress);

  public:
    float
    progress() const;
  };
}





#endif
