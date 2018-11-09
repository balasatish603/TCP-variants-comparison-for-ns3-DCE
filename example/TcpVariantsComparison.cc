#include <iostream>
#include <fstream>
#include <string>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/dce-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE ("TcpVariantsComparison");

std::string dir = "Plots/";

void
qlen (Ptr<QueueDisc> queue)
{
  uint32_t qSize = queue->GetCurrentSize ().GetValue ();
  Simulator::Schedule (Seconds (0.1), &qlen, queue);
  std::ofstream fPlotQueue (std::stringstream (dir +"/queueTraces/A.plotme").str ().c_str (), std::ios::out | std::ios::app);
  fPlotQueue << Simulator::Now ().GetSeconds () << " " << qSize << std::endl;
  fPlotQueue.close ();
}

static void GetSSStats (Ptr<Node> node, Time at, std::string stack)
{
  if(stack == "linux")
  {
    DceApplicationHelper process;
    ApplicationContainer apps;
    process.SetBinary ("ss");
    process.SetStackSize (1 << 20);
    process.AddArgument ("-a");
    process.AddArgument ("-e");
    process.AddArgument ("-i");
    apps.Add(process.Install (node));
    apps.Start(at);
  }
}

int main (int argc, char *argv[])
{
  char buffer[80];
  time_t rawtime;
  struct tm * timeinfo;
  time (&rawtime);
  timeinfo = localtime(&rawtime);

  strftime(buffer,sizeof(buffer),"%d-%m-%Y-%I-%M-%S",timeinfo);
  std::string currentTime (buffer);
  
  uint32_t mtu_bytes = 400;
  std::string bandwidth = "2Mbps";
  std::string delay = "0.01ms";
  std::string access_bandwidth = "10Mbps";
  std::string access_delay = "45ms";
  std::string queue_disc_type = "ns3::PfifoFastQueueDisc";
  bool pcap = true;
  std::string stack = "linux";
  std::string sock_factory = "ns3::TcpSocketFactory";
  float duration = 10;
  std::string transport_prot = "cubic";
  std::string transport_prot_dce = "cubic";
  float start_time = 10.1;
  bool tracing = true;
  std::string prefix_file_name = "TcpVariantsComparison";
  bool flow_monitor = false;

  //Enable checksum if linux and ns3 node communicate
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));


  CommandLine cmd;
  cmd.AddValue ("pcap", "Enable PCAP", pcap);
  cmd.AddValue ("stack", "Network stack: either ns3 or Linux", stack);
  cmd.AddValue ("transport_prot", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat, "
		"TcpLp", transport_prot);
  cmd.AddValue ("bandwidth", "Bottleneck bandwidth", bandwidth);
  cmd.AddValue ("delay", "Bottleneck delay", delay);
  cmd.AddValue ("mtu", "Size of IP packets to send in bytes", mtu_bytes);
  cmd.AddValue ("access_bandwidth", "Access link bandwidth", access_bandwidth);
  cmd.AddValue ("access_delay", "Access link delay", access_delay);
  cmd.AddValue ("start_time", "Time to start the flows", start_time);
  cmd.AddValue ("tracing", "Flag to enable/disable tracing", tracing);
  cmd.AddValue ("duration", "Time to allow flows to run in seconds", duration);
  cmd.AddValue ("queue_disc_type", "Queue disc type for gateway (e.g. ns3::CoDelQueueDisc)", queue_disc_type);
  cmd.Parse (argc, argv);
  
  transport_prot = std::string ("ns3::") + transport_prot;


  float stop_time = start_time + duration;

  NodeContainer nodes, routerNodes, linuxNodes;
  nodes.Create (3);
  linuxNodes.Add (nodes.Get (0));
  linuxNodes.Add (nodes.Get (2));
  routerNodes.Add (nodes.Get (1));
 

  PointToPointHelper link;
  link.SetDeviceAttribute ("DataRate", StringValue ("150Mbps"));
  link.SetChannelAttribute ("Delay", StringValue ("5ms"));

  PointToPointHelper bottleNeckLink;
  bottleNeckLink.SetDeviceAttribute ("DataRate", StringValue ("50Mbps"));
  bottleNeckLink.SetChannelAttribute ("Delay", StringValue ("1ms"));

  NetDeviceContainer leftToRouterDevices, bottleNeckDevices;
  leftToRouterDevices = link.Install (linuxNodes.Get (0), routerNodes.Get (0));
  bottleNeckDevices = bottleNeckLink.Install (routerNodes.Get (0), linuxNodes.Get (1));
 
  DceManagerHelper dceManager;
  LinuxStackHelper linuxStack;

  InternetStackHelper internetStack;

  if (stack == "linux")
    {
      sock_factory = "ns3::LinuxTcpSocketFactory";
      dceManager.SetNetworkStack ("ns3::LinuxSocketFdFactory",
                                  "Library", StringValue ("liblinux.so"));
      linuxStack.Install (linuxNodes);
      internetStack.Install (routerNodes);
    }
  else
    {
      internetStack.InstallAll ();
    }
    
  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");

  Ipv4InterfaceContainer sink_interfaces;  

  Ipv4InterfaceContainer interfaces;
  address.Assign (leftToRouterDevices);
  address.NewNetwork ();
  interfaces = address.Assign (bottleNeckDevices);
  sink_interfaces.Add (interfaces.Get (1));  

 // Select TCP variant
  if(stack=="ns3")
  {
    if (transport_prot.compare ("ns3::TcpWestwoodPlus") == 0)
      { 
        // TcpWestwoodPlus is not an actual TypeId name; we need TcpWestwood here
        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TcpWestwood::GetTypeId ()));
        // the default protocol type in ns3::TcpWestwood is WESTWOOD
        Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
      }
    else
      {
        TypeId tcpTid;
        NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (transport_prot, &tcpTid), "TypeId " << transport_prot << " not found");
        Config::SetDefault ("ns3::TcpL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (transport_prot)));
      }  
  }

  else if(stack=="linux")
  {
    transport_prot_dce = transport_prot.substr(8); 
    //transport_prot_dce = transport_prot.erase(0,3);
    transform(transport_prot_dce.begin(), transport_prot_dce.end(), transport_prot_dce.begin(), ::tolower);

    dceManager.Install (linuxNodes);
    dceManager.Install (routerNodes);
    linuxStack.SysctlSet (linuxNodes, ".net.ipv4.conf.default.forwarding", "1");
    linuxStack.SysctlSet (linuxNodes, ".net.ipv4.tcp_congestion_control", transport_prot_dce);

  std::cout << transport_prot_dce;
  }
  else if (stack != "ns3" && stack != "linux")
    {
      std::cout << "ERROR: " <<  stack << " is not available" << std::endl; 
    }

  LinuxStackHelper::RunIp (linuxNodes.Get (0), Seconds (0.1), "route add default via 10.0.0.2 dev sim0");
  LinuxStackHelper::RunIp (linuxNodes.Get (1), Seconds (0.1), "route add default via 10.0.1.1 dev sim0");
 
  dir += (currentTime + "/");
  std::string dirToSave = "mkdir -p " + dir;
  system (dirToSave.c_str ());
  system ((dirToSave + "/pcap/").c_str ());
  system ((dirToSave + "/queueTraces/").c_str ());

  TrafficControlHelper tchPfifo;
  tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");

  TrafficControlHelper tchCoDel;
  tchCoDel.SetRootQueueDisc ("ns3::CoDelQueueDisc");

  TrafficControlHelper tch;
  tch.SetRootQueueDisc (queue_disc_type);
  QueueDiscContainer qd;

  DataRate access_b (access_bandwidth);
  DataRate bottle_b (bandwidth);
  Time access_d (access_delay);
  Time bottle_d (delay);
  
  uint32_t size = static_cast<uint32_t>((std::min (access_b, bottle_b).GetBitRate () / 8) *
    ((access_d + bottle_d) * 2).GetSeconds ());

  Config::SetDefault ("ns3::PfifoFastQueueDisc::MaxSize",
                      QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, size / mtu_bytes)));
  Config::SetDefault ("ns3::CoDelQueueDisc::MaxSize",
                      QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, size)));

  tch.Uninstall (bottleNeckDevices.Get (0));
  
  if (queue_disc_type.compare ("ns3::PfifoFastQueueDisc") == 0)
        {
	  qd = tchPfifo.Install (bottleNeckDevices.Get (0));
        }
      else if (queue_disc_type.compare ("ns3::CoDelQueueDisc") == 0)
        {
	  qd = tchCoDel.Install (bottleNeckDevices.Get (0));
        }
      else
        {
          NS_FATAL_ERROR ("Queue not recognized. Allowed values are ns3::CoDelQueueDisc or ns3::PfifoFastQueueDisc");
        }

  
  Simulator::ScheduleNow (&qlen, qd.Get (0));

  uint16_t port = 50000;
  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));

  PacketSinkHelper sinkHelper (sock_factory, sinkLocalAddress);
  
  AddressValue remoteAddress (InetSocketAddress (sink_interfaces.GetAddress (0, 0), port));
  BulkSendHelper sender (sock_factory, Address ());
    
  sender.SetAttribute ("Remote", remoteAddress);
  ApplicationContainer sendApp = sender.Install (linuxNodes.Get (0));
  sendApp.Start (Seconds (start_time));
  sendApp.Stop (Seconds (stop_time));

  ApplicationContainer sinkApp = sinkHelper.Install (linuxNodes.Get (1));
  sinkApp.Start (Seconds (start_time));
  sinkApp.Stop (Seconds (stop_time));


  if (pcap)
    {
       std::cout << "Pcap Enabled" << std::endl;
       link.EnablePcapAll (dir + "pcap/cwnd-trace-N", true);
       bottleNeckLink.EnablePcapAll (dir + "pcap/cwnd-trace-N", true);
    }

  for ( float i = start_time; i < stop_time ; i=i+0.1)
   {
     GetSSStats(linuxNodes.Get (0), Seconds(i), stack);
   }

  Simulator::Stop (Seconds (stop_time));
  Simulator::Run ();

  QueueDisc::Stats st = qd.Get (0)->GetStats ();
  std::cout << st << std::endl;

  std::ofstream myfile;
  myfile.open (dir + "config.txt", std::fstream::in | std::fstream::out | std::fstream::app);
  myfile << "useEcn " << true << "\n";
  myfile << "queue_disc_type " << queue_disc_type << "\n";
  myfile << "transport_prot " << transport_prot << "\n";
  myfile << "stopTime " << stop_time << "\n";
  myfile.close ();

  Simulator::Destroy ();
  return 0;
}
