///*|-----------------------------------------------------------------------------
// *|            This source code is provided under the Apache 2.0 license      --
// *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
// *|                See the project's LICENSE.md for details.                  --
// *|           Copyright Thomson Reuters 2016. All rights reserved.            --
///*|-----------------------------------------------------------------------------

package com.thomsonreuters.ema.access;


import com.thomsonreuters.ema.access.OmmLoggerClient.Severity;
import com.thomsonreuters.ema.access.ProgrammaticConfigure.InstanceEntryFlag;

class OmmNiProviderConfigImpl extends EmaConfigImpl implements OmmNiProviderConfig
{
	private int 					_operationModel;
	private int                     _adminControlDirectory;
	
	OmmNiProviderConfigImpl()
	{
		super();
		channelOrChannelSet.add(ConfigManager.NiProviderChannelName);
		channelOrChannelSet.add(ConfigManager.ChannelSet);
		clear();
	}

	OmmNiProviderConfigImpl(String path)
	{
		super(path);
		channelOrChannelSet.add(ConfigManager.NiProviderChannelName);
		channelOrChannelSet.add(ConfigManager.ChannelSet);
		clear();
	}

	@Override
	public OmmNiProviderConfig clear() {
		clearInt();
		_operationModel = OperationModel.API_DISPATCH;
		_adminControlDirectory = AdminControl.API_CONTROL;
		return this;
	}

	@Override
	public OmmNiProviderConfig username(String username) {
		usernameInt(username);
		return this;
	}

	@Override
	public OmmNiProviderConfig password(String password) {
		passwordInt(password);
		return this;
	}

	@Override
	public OmmNiProviderConfig position(String position) {
		positionInt(position);
		return this;
	}

	@Override
	public OmmNiProviderConfig applicationId(String applicationId) {
		applicationIdInt(applicationId);
		return this;
	}

	@Override
	public OmmNiProviderConfig instanceId(String instanceId) {
		instanceIdInt(instanceId);
		return this;
	}

	@Override
	public OmmNiProviderConfig host(String host) {
		hostInt(host, OmmNiProviderActiveConfig.DEFAULT_NIPROVIDER_SERVICE_NAME);
		return this;
	}

	@Override
	public OmmNiProviderConfig operationModel(int operationModel) {
		_operationModel = operationModel;
		return this;
	}

	@Override
	public OmmNiProviderConfig adminControlDirectory(int control) {
		_adminControlDirectory = control;
		return this;
	}

	@Override
	public OmmNiProviderConfig providerName(String providerName) {
		
		_configSessionName = providerName;
		
		if ( _programmaticConfigure != null && _programmaticConfigure.specifyNiProviderName( providerName ) )
			return this;

		String name = (String) xmlConfig().getNiProviderAttributeValue(providerName,ConfigManager.NiProviderName);

		if ( name == null ) 
		{
			if ( providerName.equals(ActiveConfig.DEFAULT_NIPROV_NAME) )
			{
				boolean bFoundChild = xmlConfig().isNiProviderChildAvailable();
				if( bFoundChild == false )
					return this;
			}

			_configSessionName = null;
			configStrBuilder().append( "OmmNiProviderConfigImpl::providerName parameter [" )
									.append( providerName )
									.append( "] is an non-existent niprovider name" );
			throw ( oommICExcept().message( _configStrBuilder.toString()));
		}
 		else //if ( name != null ) 
		{
 			boolean bSetAttributeValue = xmlConfig().setDefaultNiProvider(name);
			if ( bSetAttributeValue == false )
			{
				xmlConfig().appendAttributeValue(ConfigManager.NIPROVIDER_GROUP, "DefaultNiProvider", ConfigManager.DefaultNiProvider,providerName);
				xmlConfig().verifyAndGetDefaultNiProvider();
			}
		}
	
		return this;
	}

	@Override
	public OmmNiProviderConfig config(Data config) {
		configInt(config);
		return this;
	}

	@Override
	public OmmNiProviderConfig addAdminMsg(ReqMsg reqMsg) {
		addAdminMsgInt(reqMsg);
		return this;
	}

	@Override
	public OmmNiProviderConfig addAdminMsg(RefreshMsg refreshMsg) {
		addAdminMsgInt(refreshMsg);
		return this;
	}
	
	@Override
	int operationModel()
	{
		return _operationModel;
	}
	
	int adminControlDirectory()
	{
		return _adminControlDirectory;
	}
	
	@Override
	String configuredName()
	{
		if (_configSessionName != null && !_configSessionName.isEmpty())
			return _configSessionName;
		
		String defaultNiProviderName = null;

		if ( _programmaticConfigure != null  && (defaultNiProviderName = _programmaticConfigure.defaultNiProvider()) != null)
			return defaultNiProviderName;
		
		defaultNiProviderName = xmlConfig().defaultNiProviderName();

		// check if default NiProvider Name and the niprovider name matched
		if ( defaultNiProviderName != null )
		{
			String checkValue = (String) xmlConfig().getNiProviderAttributeValue(defaultNiProviderName,ConfigManager.NiProviderName);

			if ( checkValue != null )
	            return defaultNiProviderName;
			else
			{		
				errorTracker().append( "default NiProvider name [" )
							.append( defaultNiProviderName )
							.append( "] is an non-existent NiProvider name; DefaultNiProvider specification ignored" )
							.create(Severity.ERROR);
				return "EmaNiProvider";
			}
		}
		
		String firstNiProviderName = (String) xmlConfig().getFirstNiProvider();
		if( firstNiProviderName != null )
			return firstNiProviderName;
		
	    return ActiveConfig.DEFAULT_NIPROV_NAME;	
	}
	
	@Override
	String channelName(String instanceName)
	{
		String channelName = null;

		if ( _programmaticConfigure != null )
		{
			channelName = _programmaticConfigure.activeEntryNames(instanceName, InstanceEntryFlag.CHANNEL_FLAG);
			if (channelName != null)
				return channelName;
			channelName = _programmaticConfigure.activeEntryNames(instanceName, InstanceEntryFlag.CHANNELSET_FLAG);
			if (channelName != null)
				return channelName;
		}

		return (String) xmlConfig().getMutualExclusiveAttribute(ConfigManager.NIPROVIDER_LIST, ConfigManager.NiProviderName, instanceName, channelOrChannelSet);
	}
	
	@Override
	String directoryName(String instanceName)
	{
		String directoryName = null;

		if ( _programmaticConfigure != null &&
			(directoryName = _programmaticConfigure.activeEntryNames(instanceName, InstanceEntryFlag.DIRECTORY_FLAG)) != null)
				return directoryName;

		directoryName = (String)xmlConfig().getNiProviderAttributeValue(instanceName,ConfigManager.NiProviderDirectoryName);
		return directoryName;
	}

	@Override
	public int providerRole()
	{
		return OmmProviderConfig.ProviderRole.NON_INTERACTIVE;
	}
}
