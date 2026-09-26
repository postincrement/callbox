#include "calls.h"

#include <ptlib/ipsock.h>

#if OPAL_SIP
#include <sip/sipep.h>
#include <sip/sipcon.h>
#include <sip/sippdu.h>
#endif
#if OPAL_H323
#include <h323/h323ep.h>
#include <h323/h323con.h>
#if OPAL_H450
#include <asn/h4502.h>
#endif
#endif
#if OPAL_IVR
#include <ep/ivr.h>
#endif
#if OPAL_VIDEO && P_VXML_VIDEO
#include <ptclib/vxml.h>
#endif
#if OPAL_HAS_MIXER
#include <ep/opalconf.h>
#endif

#if OPAL_VIDEO
#include <ptlib/videoio.h>
#if P_VIDFILE
#include <ptclib/pvidfile.h>
#endif
#endif

#include <iomanip>
#include <iostream>
#include <set>

#if defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif

static const char DefaultVxml[] =
  "<?xml version=\"1.0\"?>"
  "<vxml version=\"1.0\"><form><break time=\"3600s\"/></form></vxml>";


bool CallboxNumber::HasFeature(const PString & feature) const
{
  for (PINDEX i = 0; i < features.GetSize(); ++i) {
    if (features[i] *= feature)
      return true;
  }
  return false;
}


static PStringArray ReadStringArray(const PJSON::Object & object, const char * name)
{
  PStringArray values;
  if (!object.IsType(name, PJSON::e_Array))
    return values;

  const PJSON::Array & array = object.GetArray(name);
  for (size_t i = 0; i < array.size(); ++i) {
    PString value = array.GetString(i);
    if (!value.IsEmpty())
      values.AppendString(value);
  }
  return values;
}


static bool PrepareVideoImage(PString & image, const PDirectory & configDirectory, const PString & where)
{
  image = image.Trim();
  if (image.IsEmpty())
    return true;

  if (!PFilePath::IsAbsolutePath(image))
    image = PString(configDirectory) + image;

  PFilePath path(image);
  if (!PFile::Exists(path)) {
    std::cerr << "Video image not found for " << where << ": " << path << std::endl;
    return false;
  }

#if !OPAL_VIDEO || !P_VIDFILE
  std::cerr << "videoImage is set for " << where << ", but this build cannot play a still image as video." << std::endl;
  return false;
#else
  PVideoFile * file = PVideoFileFactory::CreateInstance(path.GetType());
  if (file == NULL) {
    std::cerr << "Video image for " << where << " must be a JPEG or BMP file: " << path << std::endl;
    return false;
  }

  bool opened = file->Open(path, PFile::ReadOnly, PFile::MustExist) && file->GetLength() == 1;
  PString error = file->GetErrorText();
  delete file;
  if (!opened) {
    std::cerr << "Cannot use " << path << " as the video image for " << where;
    if (!error.IsEmpty())
      std::cerr << ": " << error;
    std::cerr << std::endl;
    return false;
  }

  image = path;
  return true;
#endif
}


static bool ReadNumber(const PJSON::Object & object,
                       const PDirectory & configDirectory,
                       const PString & defaultVideoImage,
                       CallboxNumber & number)
{
  if (!object.IsType("alias", PJSON::e_String) || object.GetString("alias").IsEmpty()) {
    std::cerr << "Each number needs an \"alias\" string." << std::endl;
    return false;
  }
  number.alias = object.GetString("alias");
  number.audioCodecs = ReadStringArray(object, "audioCodecs");
  number.videoCodecs = ReadStringArray(object, "videoCodecs");
  number.videoImage = object.IsType("videoImage", PJSON::e_String)
    ? object.GetString("videoImage") : defaultVideoImage;
  if (!PrepareVideoImage(number.videoImage, configDirectory, number.alias))
    return false;
  number.features = ReadStringArray(object, "features");
  number.sipPort = object.IsType("sipPort", PJSON::e_Number) ? (WORD)object.GetUnsigned("sipPort") : 5060;
  number.h323Port = object.IsType("h323Port", PJSON::e_Number) ? (WORD)object.GetUnsigned("h323Port") : 1720;

  PString mode = "ivr";
  if (object.IsType("mode", PJSON::e_String) && !object.GetString("mode").IsEmpty())
    mode = object.GetString("mode");
  if (mode *= "ivr")
    number.conference = false;
  else if (mode *= "conference")
    number.conference = true;
  else {
    std::cerr << "Number " << number.alias << " mode must be \"ivr\" or \"conference\"." << std::endl;
    return false;
  }

  number.vxml = object.IsType("vxml", PJSON::e_String) ? object.GetString("vxml") : "";
  if (!number.conference && number.vxml.IsEmpty())
    number.vxml = DefaultVxml;
  else if (!number.vxml.IsEmpty() && number.vxml[0] != '<' && !PFilePath::IsAbsolutePath(number.vxml))
    number.vxml = PString(configDirectory) + number.vxml;

  if (!number.conference && !number.vxml.IsEmpty() && number.vxml[0] != '<' && !PFile::Exists(number.vxml)) {
    std::cerr << "VXML file not found for " << number.alias << ": " << number.vxml << std::endl;
    return false;
  }
  return true;
}


static PString ListenerHost(const PString & bindAddress)
{
  if (bindAddress.IsEmpty() || bindAddress == "*" || bindAddress == "0.0.0.0")
    return "*";
  return bindAddress;
}


static void AppendUnique(PStringArray & names, const PString & name)
{
  if (name.IsEmpty())
    return;
  for (PINDEX i = 0; i < names.GetSize(); ++i) {
    if (names[i] *= name)
      return;
  }
  names.AppendString(name);
}


static PString HostForUrl(const PString & host)
{
  if (host.IsEmpty() || host[0] == '[')
    return host;
  if (host.Find(':') != P_MAX_INDEX)
    return "[" + host + "]";
  return host;
}


static void CollectCallingNames(const PString & bind, PStringArray & names)
{
  PIPSocket::Address bindAddr;
  bool boundSpecific = false;
  if (bind != "*") {
    if (!bindAddr.FromString(bind))
      PIPSocket::GetHostAddress(bind, bindAddr);
    boundSpecific = bindAddr.IsValid() && !bindAddr.IsAny();
  }

  PStringArray hostnames;
  PStringArray addresses;
  PIPSocket::InterfaceTable interfaces;
  if (PIPSocket::GetInterfaceTable(interfaces)) {
    for (PINDEX i = 0; i < interfaces.GetSize(); ++i) {
      PIPSocket::Address address = interfaces[i].GetAddress();
      if (!address.IsValid() || address.IsAny() || address.IsLinkLocal() || address.IsMulticast())
        continue;
      if (boundSpecific && address != bindAddr)
        continue;

      PString ip = address.AsString(true);
      AppendUnique(addresses, ip);

      PString reverse = PIPSocket::GetHostName(address);
      if (!reverse.IsEmpty() && !(reverse *= ip) && !(reverse *= address.AsString()))
        AppendUnique(hostnames, reverse);

      if (address.IsLoopback())
        AppendUnique(hostnames, "localhost");
    }
  }

  if (!boundSpecific)
    AppendUnique(hostnames, PIPSocket::GetHostName());
  else {
    PIPSocket::Address resolved;
    if (PIPSocket::GetHostAddress(PIPSocket::GetHostName(), resolved) && resolved == bindAddr)
      AppendUnique(hostnames, PIPSocket::GetHostName());
    if (!bindAddr.FromString(bind))
      AppendUnique(hostnames, bind);
    else
      AppendUnique(addresses, bindAddr.AsString(true));
  }

  for (PINDEX i = 0; i < hostnames.GetSize(); ++i)
    AppendUnique(names, hostnames[i]);
  for (PINDEX i = 0; i < addresses.GetSize(); ++i)
    AppendUnique(names, addresses[i]);
}


static void PrintCallingAddresses(const PString & bind, const std::vector<CallboxNumber> & numbers)
{
  PStringArray names;
  CollectCallingNames(bind, names);
  if (names.IsEmpty())
    AppendUnique(names, bind == "*" ? PIPSocket::GetHostName() : bind);

  std::cout << "Call using:" << std::endl;
  for (PINDEX i = 0; i < names.GetSize(); ++i)
    std::cout << "  " << names[i] << std::endl;

  for (size_t n = 0; n < numbers.size(); ++n) {
    const CallboxNumber & number = numbers[n];
    std::cout << "  " << number.alias << " " << (number.conference ? "conference" : "ivr");
    if (!number.conference && !number.videoImage.IsEmpty())
      std::cout << " image " << number.videoImage;
    std::cout << std::endl;
    for (PINDEX i = 0; i < names.GetSize(); ++i) {
      PString host = HostForUrl(names[i]);
      if (number.sipPort != 0)
        std::cout << "    sip:" << number.alias << "@" << host << ":" << number.sipPort << std::endl;
      if (number.h323Port != 0)
        std::cout << "    h323:" << number.alias << "@" << host << ":" << number.h323Port << std::endl;
    }
  }
}


static bool StartProtocolListeners(OpalEndPoint & endpoint,
                                   const PString & host,
                                   const std::set<WORD> & ports,
                                   const char * transports)
{
  if (ports.empty())
    return true;

  PStringArray interfaces;
  PStringArray transportList = PString(transports).Tokenise(",");
  for (std::set<WORD>::const_iterator port = ports.begin(); port != ports.end(); ++port) {
    for (PINDEX t = 0; t < transportList.GetSize(); ++t) {
      PString iface = transportList[t] + "$" + host + ":" + PString(*port);
      interfaces.AppendString(iface);
    }
  }
  if (!endpoint.StartListeners(interfaces)) {
    std::cerr << "Cannot listen for " << endpoint.GetPrefixName() << " on " << interfaces << std::endl;
    return false;
  }
  std::cout << endpoint.GetPrefixName() << " listening on "
            << std::setfill(',') << endpoint.GetListeners() << std::setfill(' ') << std::endl;
  return true;
}


static void AddPartyAlias(PStringArray & aliases, const PString & raw)
{
  PString user = raw.Trim();
  if (user.IsEmpty())
    return;

  PINDEX colon = user.Find(':');
  if (colon != P_MAX_INDEX && user.Left(colon).Find('.') == P_MAX_INDEX)
    user = user.Mid(colon + 1);
  PINDEX at = user.Find('@');
  if (at != P_MAX_INDEX)
    user = user.Left(at);
  PINDEX semi = user.FindOneOf(";>");
  if (semi != P_MAX_INDEX)
    user = user.Left(semi);
  user = user.Trim();
  if (!user.IsEmpty())
    aliases.AppendString(user);
}


static WORD ExplicitPort(const PString & raw)
{
  PString host = raw;
  PINDEX at = host.Find('@');
  if (at != P_MAX_INDEX)
    host = host.Mid(at + 1);
  PINDEX semi = host.Find(';');
  if (semi != P_MAX_INDEX)
    host = host.Left(semi);
  host = host.Trim();
  if (host.IsEmpty())
    return 0;

  if (host[0] == '[') {
    PINDEX end = host.Find(']');
    if (end == P_MAX_INDEX || end + 1 >= host.GetLength() || host[end + 1] != ':')
      return 0;
    return (WORD)host.Mid(end + 2).AsUnsigned();
  }

  PINDEX colon = host.Find(':');
  if (colon == P_MAX_INDEX)
    return 0;
  return (WORD)host.Mid(colon + 1).AsUnsigned();
}


static bool CodecListed(const PString & name, const PStringArray & list)
{
  for (PINDEX i = 0; i < list.GetSize(); ++i) {
    PString pattern = list[i];
    if (pattern.IsEmpty())
      continue;

    PINDEX star = pattern.Find('*');
    if (star != P_MAX_INDEX) {
      if (name.Left(star) *= pattern.Left(star))
        return true;
      continue;
    }

    if (name *= pattern)
      return true;

    // OPAL names each member of a family with a hyphen, for example "H.264-1".
    if (name.GetLength() > pattern.GetLength() && name[pattern.GetLength()] == '-') {
      PString prefix = name.Left(pattern.GetLength());
      if (prefix *= pattern)
        return true;
    }
  }
  return false;
}


#if OPAL_SIP

class CallboxSIPEndPoint;

class CallboxSIPConnection : public SIPConnection
{
  PCLASSINFO(CallboxSIPConnection, SIPConnection);
public:
  CallboxSIPConnection(const Init & init, CallboxCalls & calls)
    : SIPConnection(init)
    , m_calls(calls)
  {
  }

  virtual void OnReceivedREFER(SIP_PDU & request) override;
  virtual void OnReceivedReINVITE(SIP_PDU & request) override;

private:
  CallboxCalls & m_calls;
};


class CallboxSIPEndPoint : public SIPEndPoint
{
  PCLASSINFO(CallboxSIPEndPoint, SIPEndPoint);
public:
  explicit CallboxSIPEndPoint(CallboxCalls & calls)
    : SIPEndPoint(calls)
    , m_calls(calls)
  {
  }

  virtual SIPConnection * CreateConnection(const SIPConnection::Init & init) override
  {
    return new CallboxSIPConnection(init, m_calls);
  }

private:
  CallboxCalls & m_calls;
};


static bool SipOfferIsHold(const PString & body)
{
  PCaselessString text = body;
  return text.Find("a=sendonly") != P_MAX_INDEX || text.Find("a=inactive") != P_MAX_INDEX;
}


void CallboxSIPConnection::OnReceivedREFER(SIP_PDU & request)
{
  if (m_calls.AllowsFeature(*this, "transfer")) {
    SIPConnection::OnReceivedREFER(request);
    return;
  }

  PTRACE(2, "Refusing REFER; transfer is not enabled for " << *this);
  SIPTransaction * response = new SIPResponse(GetEndPoint(), request, SIP_PDU::Failure_Forbidden);
  response->Send();
}


void CallboxSIPConnection::OnReceivedReINVITE(SIP_PDU & request)
{
  if (SipOfferIsHold(request.GetEntityBody()) && !m_calls.AllowsFeature(*this, "hold")) {
    PTRACE(2, "Refusing hold re-INVITE; hold is not enabled for " << *this);
    request.SendResponse(SIP_PDU::Failure_NotAcceptableHere);
    return;
  }
  SIPConnection::OnReceivedReINVITE(request);
}

#endif // OPAL_SIP


#if OPAL_H323

class CallboxH323Connection : public H323Connection
{
  PCLASSINFO(CallboxH323Connection, H323Connection);
public:
  CallboxH323Connection(OpalCall & call,
                        H323EndPoint & endpoint,
                        const PString & token,
                        const PString & alias,
                        const H323TransportAddress & address,
                        unsigned options,
                        OpalConnection::StringOptions * stringOptions,
                        CallboxCalls & calls)
    : H323Connection(call, endpoint, token, alias, address, options, stringOptions)
    , m_calls(calls)
  {
  }

#if OPAL_H450
  virtual void HandleTransferCall(const PString & token, const PString & identity) override;
#endif

private:
  CallboxCalls & m_calls;
};


class CallboxH323EndPoint : public H323EndPoint
{
  PCLASSINFO(CallboxH323EndPoint, H323EndPoint);
public:
  explicit CallboxH323EndPoint(CallboxCalls & calls)
    : H323EndPoint(calls)
    , m_calls(calls)
  {
  }

  void AttachConfiguredCapabilities(const PStringArray & codecs);

  virtual H323Connection * CreateConnection(OpalCall & call,
                                            const PString & token,
                                            void * userData,
                                            OpalTransport & transport,
                                            const PString & alias,
                                            const H323TransportAddress & address,
                                            H323SignalPDU * setupPDU,
                                            unsigned options = 0,
                                            OpalConnection::StringOptions * stringOptions = NULL) override
  {
    (void)userData;
    (void)transport;
    (void)setupPDU;
    return new CallboxH323Connection(call, *this, token, alias, address, options, stringOptions, m_calls);
  }

private:
  CallboxCalls & m_calls;
};


void CallboxH323EndPoint::AttachConfiguredCapabilities(const PStringArray & codecs)
{
  if (codecs.IsEmpty())
    return;

  PStringArray attached;
  H323CapabilityFactory::KeyList_T keys = H323CapabilityFactory::GetKeyList();
  for (H323CapabilityFactory::KeyList_T::const_iterator it = keys.begin(); it != keys.end(); ++it) {
    PString name(*it);
    if (!CodecListed(name, codecs))
      continue;
    if (m_capabilities.FindCapability(name, H323Capability::e_Unknown, true) == NULL)
      m_capabilities.AddAllCapabilities(0, 0, name, true);
    attached.AppendString(name);
    PTRACE(2, "H323\tAttached capability " << name);
    std::cout << "H.323 capability " << name << std::endl;
  }

  for (PINDEX i = 0; i < codecs.GetSize(); ++i) {
    if (codecs[i].IsEmpty() || CodecListed(codecs[i], attached))
      continue;
    bool matched = false;
    for (PINDEX a = 0; a < attached.GetSize(); ++a) {
      PStringArray one;
      one.AppendString(codecs[i]);
      if (CodecListed(attached[a], one)) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      PTRACE(2, "H323\tNo capability for configured codec " << codecs[i]);
      std::cout << "No H.323 capability for configured codec " << codecs[i] << std::endl;
    }
  }
}


#if OPAL_H450
void CallboxH323Connection::HandleTransferCall(const PString & token, const PString & identity)
{
  if (m_calls.AllowsFeature(*this, "transfer")) {
    H323Connection::HandleTransferCall(token, identity);
    return;
  }
  PTRACE(2, "Refusing H.450 transfer; transfer is not enabled for " << *this);
  HandleCallTransferFailure(H4502_CallTransferErrors::e_unspecified);
}
#endif

#endif // OPAL_H323


#if OPAL_IVR

class CallboxIVRConnection : public OpalIVRConnection
{
  PCLASSINFO(CallboxIVRConnection, OpalIVRConnection);
public:
  CallboxIVRConnection(OpalCall & call,
                       OpalIVREndPoint & endpoint,
                       void * userData,
                       const PString & vxml,
                       unsigned options,
                       OpalConnection::StringOptions * stringOptions)
    : OpalIVRConnection(call, endpoint, userData, vxml, options, stringOptions)
  {
  }

  virtual void OnApplyStringOptions() override
  {
    OpalIVRConnection::OnApplyStringOptions();

#if OPAL_VIDEO
    // OpalIVRConnection forces video to DontOffer after the string options
    // are stored, and a later AutoStart add will not replace that entry.
    PString autoStart = m_stringOptions(OPAL_OPT_AUTO_START);
    if (autoStart.Find("video:sendrecv") != P_MAX_INDEX || autoStart.Find("video:yes") != P_MAX_INDEX)
      m_autoStartInfo[OpalMediaType::Video()] = OpalMediaType::ReceiveTransmit;

#if P_VXML_VIDEO && P_VIDFILE
    PString image = m_stringOptions(OPAL_OPT_VIDEO_INPUT_DEVICE);
    if (!image.IsEmpty()) {
      PVideoDevice::OpenArgs args;
      args.driverName = m_stringOptions(OPAL_OPT_VIDEO_INPUT_DRIVER);
      if (args.driverName.IsEmpty())
        args.driverName = P_VIDEO_FILE_DRIVER;
      args.deviceName = image;
      PVideoInputDevice * device = PVideoInputDevice::CreateOpenedDevice(args);
      PVideoInputDeviceIndirect * sender =
        dynamic_cast<PVideoInputDeviceIndirect *>(&GetVXMLSession().GetVideoSender());
      if (device != NULL && sender != NULL)
        sender->SetActualDevice(device);
      else
        delete device;
    }
#endif
#endif
  }
};


class CallboxIVREndPoint : public OpalIVREndPoint
{
  PCLASSINFO(CallboxIVREndPoint, OpalIVREndPoint);
public:
  explicit CallboxIVREndPoint(OpalManager & manager)
    : OpalIVREndPoint(manager)
  {
  }

  virtual OpalIVRConnection * CreateConnection(OpalCall & call,
                                               void * userData,
                                               const PString & vxml,
                                               unsigned options,
                                               OpalConnection::StringOptions * stringOptions) override
  {
    return new CallboxIVRConnection(call, *this, userData, vxml, options, stringOptions);
  }
};

#endif // OPAL_IVR


CallboxCalls::CallboxCalls(CallboxDatabase & database)
  : m_database(database)
  , m_callLogStdout(false)
{
}


void CallboxCalls::SetCallLog(bool toStdout, const PString & path)
{
  m_callLogStdout = toStdout;
  m_callLogPath = path;
}


bool CallboxCalls::OpenCallLog()
{
  if (m_callLogPath.IsEmpty())
    return true;

  PFilePath path(m_callLogPath);
  PDirectory directory(path.GetDirectory());
  if (!directory.Exists() && !directory.Create(PFileInfo::DefaultDirPerms, true)) {
    std::cerr << "Cannot create call log directory " << directory << std::endl;
    return false;
  }
  if (!m_callLogFile.Open(path, PFile::WriteOnly, PFile::Create)) {
    std::cerr << "Cannot open call log " << path << ": " << m_callLogFile.GetErrorText() << std::endl;
    return false;
  }
  m_callLogFile.SetPosition(0, PFile::End);
  return true;
}


void CallboxCalls::WriteCallLog(const char * event,
                                const ActiveCall & active,
                                const CallboxNumber & number,
                                const PString & detail)
{
  if (!m_callLogStdout && !m_callLogFile.IsOpen())
    return;

  PStringStream line;
  line << PTime().AsString(PTime::LoggingFormat)
       << " call " << event
       << " " << active.callerUri
       << " -> " << number.alias
       << " " << active.protocol
       << " " << (number.conference ? "conference" : "ivr");
  if (!detail.IsEmpty())
    line << " " << detail;

  PWaitAndSignal lock(m_callLogMutex);
  if (m_callLogStdout) {
    std::cout << line << std::endl;
    std::cout.flush();
  }
  if (m_callLogFile.IsOpen()) {
    m_callLogFile << line << '\n';
    m_callLogFile.flush();
  }
}


bool CallboxCalls::Start(const PJSON::Object & config, const PDirectory & configDirectory)
{
  if (!config.IsType("numbers", PJSON::e_Array) || config.GetArray("numbers").empty()) {
    std::cerr << "Configuration needs a non-empty \"numbers\" array." << std::endl;
    return false;
  }

  PString defaultVideoImage;
  if (config.IsType("videoImage", PJSON::e_String))
    defaultVideoImage = config.GetString("videoImage");
  if (!PrepareVideoImage(defaultVideoImage, configDirectory, "the configuration"))
    return false;

  m_numbers.clear();
  const PJSON::Array & numberArray = config.GetArray("numbers");
  bool wantIvr = false;
  bool wantConference = false;
  for (size_t i = 0; i < numberArray.size(); ++i) {
    if (!numberArray.IsObject(i)) {
      std::cerr << "Number entry " << i << " is not an object." << std::endl;
      return false;
    }
    CallboxNumber number;
    if (!ReadNumber(numberArray.GetObject(i), configDirectory, defaultVideoImage, number))
      return false;
    for (size_t earlier = 0; earlier < m_numbers.size(); ++earlier) {
      if (m_numbers[earlier].alias *= number.alias) {
        std::cerr << "Alias \"" << number.alias << "\" is listed more than once." << std::endl;
        return false;
      }
    }
    wantIvr = wantIvr || !number.conference;
    wantConference = wantConference || number.conference;
    m_numbers.push_back(number);
  }

#if !OPAL_IVR
  if (wantIvr) {
    std::cerr << "An IVR number is configured, but this OPAL build has no IVR." << std::endl;
    return false;
  }
#endif
#if !OPAL_HAS_MIXER
  if (wantConference) {
    std::cerr << "A conference number is configured, but this OPAL build has no mixer." << std::endl;
    return false;
  }
#endif

  PString bindAddress = ListenerHost(config.IsType("bindAddress", PJSON::e_String)
                                     ? config.GetString("bindAddress") : "");
  std::set<WORD> sipPorts;
  std::set<WORD> h323Ports;
  PStringArray codecOrder;
  for (size_t i = 0; i < m_numbers.size(); ++i) {
    if (m_numbers[i].sipPort != 0)
      sipPorts.insert(m_numbers[i].sipPort);
    if (m_numbers[i].h323Port != 0)
      h323Ports.insert(m_numbers[i].h323Port);
    for (PINDEX c = 0; c < m_numbers[i].audioCodecs.GetSize(); ++c)
      codecOrder.AppendString(m_numbers[i].audioCodecs[c]);
    for (PINDEX c = 0; c < m_numbers[i].videoCodecs.GetSize(); ++c)
      codecOrder.AppendString(m_numbers[i].videoCodecs[c]);
  }

  if (!codecOrder.IsEmpty())
    SetMediaFormatOrder(codecOrder);

#if OPAL_IVR
  if (wantIvr)
    new CallboxIVREndPoint(*this);
#else
  (void)wantIvr;
#endif

#if OPAL_HAS_MIXER
  if (wantConference) {
    OpalConfEndPoint * conference = new OpalConfEndPoint(*this);
    for (size_t i = 0; i < m_numbers.size(); ++i) {
      if (!m_numbers[i].conference)
        continue;
      OpalConfNodeInfo info;
      info.m_name = m_numbers[i].alias;
#if OPAL_VIDEO
      info.m_audioOnly = m_numbers[i].videoCodecs.IsEmpty() && m_numbers[i].videoImage.IsEmpty();
#endif
      if (conference->AddNode(info) == NULL) {
        std::cerr << "Cannot create conference \"" << m_numbers[i].alias << "\"." << std::endl;
        return false;
      }
    }
  }
#else
  (void)wantConference;
#endif

#if OPAL_SIP
  SIPEndPoint * sip = new CallboxSIPEndPoint(*this);
  sip->SetDefaultLocalPartyName(m_numbers[0].alias);
  if (!StartProtocolListeners(*sip, bindAddress, sipPorts, "udp,tcp"))
    return false;
#else
  if (!sipPorts.empty())
    std::cout << "SIP listeners skipped; this OPAL build has no SIP." << std::endl;
#endif

#if OPAL_H323
  CallboxH323EndPoint * h323 = new CallboxH323EndPoint(*this);
  h323->AttachConfiguredCapabilities(codecOrder);
  h323->SetDefaultLocalPartyName(m_numbers[0].alias);
  for (size_t i = 1; i < m_numbers.size(); ++i)
    h323->AddAliasName(m_numbers[i].alias);
  if (!StartProtocolListeners(*h323, bindAddress, h323Ports, "tcp"))
    return false;
#else
  if (!h323Ports.empty())
    std::cout << "H.323 listeners skipped; this OPAL build has no H.323." << std::endl;
#endif

  PrintCallingAddresses(bindAddress, m_numbers);
  return true;
}


void CallboxCalls::Stop()
{
  ShutDownEndpoints();
}


bool CallboxCalls::AllowsFeature(const OpalConnection & connection, const PString & feature) const
{
  PWaitAndSignal lock(m_callMutex);
  const ActiveCall * active = FindActive(connection);
  if (active == NULL || active->index >= m_numbers.size())
    return false;
  return m_numbers[active->index].HasFeature(feature);
}


PBoolean CallboxCalls::OnIncomingConnection(OpalConnection & connection,
                                            unsigned options,
                                            OpalConnection::StringOptions * stringOptions)
{
  if (connection.IsNetworkConnection() && connection.GetOtherPartyConnection() == NULL) {
    int index = MatchNumber(connection);
    if (index < 0) {
      std::cerr << "No number matches " << connection.GetRemotePartyURL()
                << " calling " << connection.GetDestinationAddress() << std::endl;
      return false;
    }

    const CallboxNumber & number = m_numbers[(size_t)index];
    ActiveCall active;
    active.index = (size_t)index;
    active.localPort = ExplicitPort(connection.GetLocalPartyURL());
    active.startedAt = PTime().AsString(PTime::LoggingFormat);
    active.callerUri = connection.GetRemotePartyURL();
    active.callerAddress = connection.GetRemoteAddress();
    active.protocol = connection.GetPrefixName();
    {
      PWaitAndSignal lock(m_callMutex);
      m_active[connection.GetCall().GetToken()] = active;
    }

#if OPAL_VIDEO
    OpalConnection::StringOptions mediaOptions = connection.GetStringOptions();
    if (stringOptions != NULL)
      mediaOptions.Merge(*stringOptions, PStringOptions::e_MergeOverwrite);
    if (!number.videoCodecs.IsEmpty() || !number.videoImage.IsEmpty()) {
      PString autoStart = mediaOptions(OPAL_OPT_AUTO_START);
      if (autoStart.Find("video:") == P_MAX_INDEX)
        autoStart &= "video:sendrecv\n";
      mediaOptions.SetAt(OPAL_OPT_AUTO_START, autoStart);
    }
    if (!number.conference && !number.videoImage.IsEmpty()) {
#if P_VIDFILE
      mediaOptions.SetAt(OPAL_OPT_VIDEO_INPUT_DEVICE, number.videoImage);
      mediaOptions.SetAt(OPAL_OPT_VIDEO_INPUT_DRIVER, P_VIDEO_FILE_DRIVER);
#endif
      PVideoDevice::OpenArgs display;
      display.driverName = P_NULL_VIDEO_DRIVER;
      display.deviceName = P_NULL_VIDEO_DEVICE;
      SetVideoOutputDevice(display);
      SetVideoPreviewDevice(display);
    }
    connection.SetStringOptions(mediaOptions, true, false);
#endif

    PString destination = number.conference
      ? PString("mcu:") + number.alias
      : PString("ivr:") + number.vxml;
    connection.GetCall().SetPartyB(destination);
    std::cout << "Answering " << active.callerUri << " as " << number.alias
              << " (" << (number.conference ? "conference" : "ivr") << ")" << std::endl;
  }

  return OpalManager::OnIncomingConnection(connection, options, stringOptions);
}


void CallboxCalls::OnEstablished(OpalConnection & connection)
{
  ActiveCall accepted;
  bool logAccepted = false;
  if (connection.IsNetworkConnection()) {
    PWaitAndSignal lock(m_callMutex);
    std::map<PString, ActiveCall>::iterator it = m_active.find(connection.GetCall().GetToken());
    if (it != m_active.end()) {
      if (it->second.answeredAt.IsEmpty()) {
        it->second.answeredAt = PTime().AsString(PTime::LoggingFormat);
        accepted = it->second;
        logAccepted = true;
      }
      RememberCodecs(connection, it->second);
    }
  }
  if (logAccepted && accepted.index < m_numbers.size())
    WriteCallLog("accepted", accepted, m_numbers[accepted.index], "");
  OpalManager::OnEstablished(connection);
}


void CallboxCalls::OnClearedCall(OpalCall & call)
{
  ActiveCall active;
  bool known = false;
  {
    PWaitAndSignal lock(m_callMutex);
    std::map<PString, ActiveCall>::iterator it = m_active.find(call.GetToken());
    if (it != m_active.end()) {
      active = it->second;
      m_active.erase(it);
      known = true;
    }
  }

  if (known && active.index < m_numbers.size()) {
    const CallboxNumber & number = m_numbers[active.index];
    CallboxCallRecord record;
    record.startedAt = active.startedAt;
    record.answeredAt = active.answeredAt;
    record.endedAt = PTime().AsString(PTime::LoggingFormat);
    record.protocol = active.protocol;
    record.callerUri = active.callerUri;
    record.callerAddress = active.callerAddress;
    record.calleeAlias = number.alias;
    record.localPort = active.localPort;
    record.audioCodec = active.audioCodec;
    record.videoCodec = active.videoCodec;
    record.endReason = call.GetCallEndReasonText();
    record.mode = number.conference ? "conference" : "ivr";
    m_database.SaveCall(record);
    WriteCallLog("ended", active, number, record.endReason);
  }

  OpalManager::OnClearedCall(call);
}


void CallboxCalls::AdjustMediaFormats(bool local,
                                      const OpalConnection & connection,
                                      OpalMediaFormatList & mediaFormats) const
{
  const CallboxNumber * number = NULL;
  {
    PWaitAndSignal lock(m_callMutex);
    const ActiveCall * active = FindActive(connection);
    if (active != NULL && active->index < m_numbers.size())
      number = &m_numbers[active->index];
  }

  if (number != NULL) {
    PStringArray remove;
    for (OpalMediaFormatList::iterator format = mediaFormats.begin(); format != mediaFormats.end(); ++format) {
      OpalMediaType type = format->GetMediaType();
      // YUV420P and PCM-16 are the internal formats the IVR and conference
    // transcode from. They are not named in videoCodecs or audioCodecs.
    if (!format->IsTransportable())
      continue;
    if (type == OpalMediaType::Audio() && !number->audioCodecs.IsEmpty() &&
          !CodecListed(format->GetName(), number->audioCodecs))
        remove.AppendString(format->GetName());
      else if (type == OpalMediaType::Video() && !number->videoCodecs.IsEmpty() &&
               !CodecListed(format->GetName(), number->videoCodecs))
        remove.AppendString(format->GetName());
    }
    if (!remove.IsEmpty())
      mediaFormats.Remove(remove);
  }

  OpalManager::AdjustMediaFormats(local, connection, mediaFormats);
}


int CallboxCalls::MatchNumber(OpalConnection & connection) const
{
  PString prefix = connection.GetPrefixName();
  bool sip = (prefix *= "sip") || (prefix *= "sips");
  bool h323 = prefix.NumCompare("h323") == EqualTo;
  if (!sip && !h323)
    return -1;

  PStringArray aliases;
  AddPartyAlias(aliases, connection.GetCalledPartyNumber());
  AddPartyAlias(aliases, connection.GetCalledPartyName());
  AddPartyAlias(aliases, connection.GetDestinationAddress());
  AddPartyAlias(aliases, connection.GetLocalPartyURL());

  for (size_t i = 0; i < m_numbers.size(); ++i) {
    for (PINDEX a = 0; a < aliases.GetSize(); ++a) {
      if (m_numbers[i].alias *= aliases[a])
        return (int)i;
    }
  }

  WORD port = ExplicitPort(connection.GetLocalPartyURL());
  if (port == 0)
    return -1;

  int found = -1;
  int count = 0;
  for (size_t i = 0; i < m_numbers.size(); ++i) {
    WORD numberPort = sip ? m_numbers[i].sipPort : m_numbers[i].h323Port;
    if (numberPort != 0 && numberPort == port) {
      found = (int)i;
      ++count;
    }
  }
  return count == 1 ? found : -1;
}


void CallboxCalls::RememberCodecs(OpalConnection & connection, ActiveCall & active) const
{
  OpalMediaStreamPtr audio = connection.GetMediaStream(OpalMediaType::Audio(), false);
  if (audio == NULL)
    audio = connection.GetMediaStream(OpalMediaType::Audio(), true);
  if (audio != NULL)
    active.audioCodec = audio->GetMediaFormat().GetName();

  OpalMediaStreamPtr video = connection.GetMediaStream(OpalMediaType::Video(), false);
  if (video == NULL)
    video = connection.GetMediaStream(OpalMediaType::Video(), true);
  if (video != NULL)
    active.videoCodec = video->GetMediaFormat().GetName();
}


const CallboxCalls::ActiveCall * CallboxCalls::FindActive(const OpalConnection & connection) const
{
  std::map<PString, ActiveCall>::const_iterator it = m_active.find(connection.GetCall().GetToken());
  if (it == m_active.end())
    return NULL;
  return &it->second;
}
