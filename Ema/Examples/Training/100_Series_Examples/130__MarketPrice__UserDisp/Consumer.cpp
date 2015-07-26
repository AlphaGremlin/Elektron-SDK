///*|-----------------------------------------------------------------------------
// *|            This source code is provided under the Apache 2.0 license      --
// *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
// *|                See the project's LICENSE.md for details.                  --
// *|           Copyright Thomson Reuters 2015. All rights reserved.            --
///*|-----------------------------------------------------------------------------

#include "Consumer.h"

using namespace thomsonreuters::ema::access;
using namespace std;

void AppClient::onRefreshMsg( const RefreshMsg& refreshMsg, const OmmConsumerEvent& ) 
{
	if ( refreshMsg.hasMsgKey() )
		cout << endl << "Item Name: " << refreshMsg.getName() << endl << "Service Name: " << refreshMsg.getServiceName();

	cout << endl << "Item State: " << refreshMsg.getState().toString() << endl;

	if ( DataType::FieldListEnum == refreshMsg.getPayload().getDataType() )
		decode( refreshMsg.getPayload().getFieldList() );
}

void AppClient::onUpdateMsg( const UpdateMsg& updateMsg, const OmmConsumerEvent& ) 
{
	if ( updateMsg.hasMsgKey() )
		cout << endl << "Item Name: " << updateMsg.getName() << endl << "Service Name: " << updateMsg.getServiceName() << endl;

	if ( DataType::FieldListEnum == updateMsg.getPayload().getDataType() )
		decode( updateMsg.getPayload().getFieldList() );
}

void AppClient::onStatusMsg( const StatusMsg& statusMsg, const OmmConsumerEvent& ) 
{
	if ( statusMsg.hasMsgKey() )
		cout << endl << "Item Name: " << statusMsg.getName() << endl << "Service Name: " << statusMsg.getServiceName();

	if ( statusMsg.hasState() )
		cout << endl << "Item State: " << statusMsg.getState().toString() << endl;
}

void AppClient::decode( const FieldList& fl )
{
	while ( !fl.forth() )
		cout << "Fid: " << fl.getEntry().getFieldId() << " Name: " << fl.getEntry().getName() << " value: " << fl.getEntry().getLoad().toString() << endl;
}

int main( int argc, char* argv[] )
{ 
	try { 
		AppClient client;
		OmmConsumer consumer( OmmConsumerConfig().operationModel( OmmConsumerConfig::UserDispatchEnum ).host( "localhost:14002" ).username( "user" ) );
		consumer.registerClient( ReqMsg().serviceName( "DIRECT_FEED" ).name( "IBM.N" ), client );
		unsigned long long startTime = getCurrentTime();
		while ( startTime + 60000 > getCurrentTime() )
			consumer.dispatch( 10 );		// calls to onRefreshMsg(), onUpdateMsg(), or onStatusMsg() execute on this thread
	} catch ( const OmmException& excp ) {
		cout << excp << endl;
	}
	return 0;
}