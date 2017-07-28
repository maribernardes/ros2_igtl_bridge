/*=========================================================================

  Program:   ROS-IGTL-Bridge Node
  Language:  C++

  Copyright (c) Brigham and Women's Hospital. All rights reserved.

  This software is distributed WITHOUT ANY WARRANTY; without even
  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

#include <boost/thread.hpp>

#include "ros_igtl_bridge.h"

#include "rib_converter_point.h"
#include "rib_converter_pointcloud.h"
#include "rib_converter_transform.h"
#include "rib_converter_polydata.h"
#include "rib_converter_string.h"
#include "rib_converter_image.h"


//----------------------------------------------------------------------
ROS_IGTL_Bridge::ROS_IGTL_Bridge(int argc, char *argv[], const char* node_name)
{
  ros::init(argc, argv, node_name);
  nh = new ros::NodeHandle;	
  
  // run bridge as client or server
  std::string type;
  
  ROS_INFO("[ROS-IGTL-Bridge] a");
  if(nh->getParam("/RIB_type",type))
    {
    ROS_INFO("[ROS-IGTL-Bridge] b ");
    if(type == "client")
      ConnectToIGTLServer();		
    else if(type == "server")
      CreateIGTLServer();
    else
      ROS_ERROR("[ROS-IGTL-Bridge] Unknown Value for Parameter 'RIB_type'");	
    }
  else
    {
    short srvcl = 0;
    while(1)
      {
      std::cout << "[ROS-IGTL-Bridge] Please type <1> or <2> to run node as OpenIGTLink client or server"<<std::endl;
      std::cout << "1 : SERVER" << std::endl << "2 : CLIENT" << std::endl;
      std::cin>>srvcl;
      
      if (srvcl==1)
        {
          CreateIGTLServer();
          break;
        }
      else if (srvcl==2)
        {
          ConnectToIGTLServer();
          break;
        }
      else
        {
        ROS_ERROR("[ROS-IGTL-Bridge] Invalid answer.");
        }
      }
    }
  
  ROS_INFO("[ROS-IGTL-Bridge] ROS-IGTL-Bridge up and Running.");
  
  RIBConverterPoint * point = new RIBConverterPoint;
  this->AddConverter(point, 10, "IGTL_POINT_IN", "IGTL_POINT_OUT");
  
  RIBConverterTransform* transform = new RIBConverterTransform;
  this->AddConverter(transform, 10, "IGTL_TRANSFORM_IN", "IGTL_TRANSFORM_OUT");
  
  RIBConverterPolyData* polydata = new RIBConverterPolyData;
  this->AddConverter(polydata, 10, "IGTL_POLYDATA_IN", "IGTL_POLYDATA_OUT");

  RIBConverterString* string = new RIBConverterString;
  this->AddConverter(string, 10, "IGTL_STRING_IN", "IGTL_STRING_OUT");

  RIBConverterImage* image = new RIBConverterImage;
  this->AddConverter(image, 10, "IGTL_IMAGE_IN", "IGTL_IMAGE_OUT");

  RIBConverterPointCloud* pointcloud = new RIBConverterPointCloud;
  this->AddConverter(pointcloud, 10, "IGTL_POINTCLOUD_IN", "IGTL_POINTCLOUD_OUT");

  // start receiver thread
  boost::thread* receiver_thread = new boost::thread(boost::bind(&ROS_IGTL_Bridge::IGTLReceiverThread, this));  
}

//----------------------------------------------------------------------
ROS_IGTL_Bridge::~ROS_IGTL_Bridge()
{
  socket->CloseSocket();
}

//----------------------------------------------------------------------
void ROS_IGTL_Bridge::Run()
{
  ros::spin();
}

//----------------------------------------------------------------------
igtl::Socket::Pointer ROS_IGTL_Bridge::GetSocketPointer()
{
  igtl::Socket::Pointer socket_ptr = static_cast<igtl::Socket::Pointer>(socket);
  return socket_ptr;
}


//----------------------------------------------------------------------
void ROS_IGTL_Bridge::CreateIGTLServer()
{
  int    port     = 18944;  // std port
  if(nh->getParam("/RIB_port",port))
    {}
  else
    {
    ROS_INFO("[ROS-IGTL-Bridge] Input socket port: ");
    std::cin >> port;
    }
  igtl::ServerSocket::Pointer serverSocket;
  serverSocket = igtl::ServerSocket::New();
  int c = serverSocket->CreateServer(port);
  
  if (c < 0)
    {
    ROS_ERROR("[ROS-IGTL-Bridge] Cannot create a server socket.");
    }
  ROS_INFO("[ROS-IGTL-Bridge] Server socket created. Please connect to port: %d",port);
  
  // wait for connection
  while (1)
    {
    socket = serverSocket->WaitForConnection(1000);
    if (ROS_IGTL_Bridge::socket.IsNotNull()) 
      {   
      break;
      }
    }
}

//----------------------------------
void ROS_IGTL_Bridge::ConnectToIGTLServer()
{
  igtl::ClientSocket::Pointer clientsocket;
  clientsocket = igtl::ClientSocket::New();
  
  int    port     = 18944; // std port
  std::string ip;
  // get ip
  if(nh->getParam("/RIB_server_ip",ip))
    {}
  else
    {
    ROS_INFO("[ROS-IGTL-Bridge] Please enter ServerIP: ");
    std::cin >> ip;
    }
  // get port
  if(nh->getParam("/RIB_port",port))
    {}
  else
    {
    ROS_INFO("[ROS-IGTL-Bridge] Please enter ServerPort:  ");
    std::cin >> port;
    }
  // connect to server
  int r = clientsocket->ConnectToServer(ip.c_str(), port);
  
  if (r != 0)
    {
    ROS_ERROR("[ROS-IGTL-Bridge] Cannot connect to server.");
    exit(0);
    }
  socket = (igtl::Socket *)(clientsocket);
}

// ----- receiving from slicer -----------------------------------------
//----------------------------------------------------------------------
void ROS_IGTL_Bridge::IGTLReceiverThread()
{
  igtl::MessageHeader::Pointer headerMsg;
  headerMsg = igtl::MessageHeader::New();
  int rs = 0;
  while(1)
    {
    headerMsg->InitPack();
    // receive packet
    rs = socket->Receive(headerMsg->GetPackPointer(), headerMsg->GetPackSize());
    
    if (rs == 0)
      socket->CloseSocket();
    if (rs != headerMsg->GetPackSize())
      continue;
    
    headerMsg->Unpack();
    
    std::vector< RIBConverterBase* >::iterator iter;
    for (iter = this->converters.begin(); iter != this->converters.end(); iter ++)
      {
        if (strcmp(headerMsg->GetDeviceType(), (*iter)->messageTypeString()) == 0)
          {
            (*iter)->onIGTLMessage(headerMsg);
            break;
          }
      }
    if (iter == this->converters.end())
      {
        socket->Skip(headerMsg->GetBodySizeToRead(),0);
      }
    }
}


void ROS_IGTL_Bridge::AddConverter(RIBConverterBase* converter, uint32_t size, const char* topicPublish, const char* topicSubscribe)
{
  std::cerr << "void ROS_IGTL_Bridge::AddConverter() topic = " << topicPublish << std::endl;
  converter->setup(this->nh, this->socket, size);
  converter->publish(topicPublish);
  converter->subscribe(topicSubscribe);
  this->converters.push_back(converter);
}


