// Copyright (c) 2009-2012 The Darkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "protocol.h"
#include "activemasternode.h"
#include "masternodeman.h"
#include <boost/lexical_cast.hpp>
#include "clientversion.h"

//
// Bootup the masternode, look for a 500 AxC input and register on the network
//
void CActiveMasternode::ManageStatus()
{
    std::string errorMessage;

    if (fDebug) LogPrintf("CActiveMasternode::ManageStatus() - Begin\n");

    if(!fMasterNode) return;

    //need correct adjusted time to send ping
    bool fIsInitialDownload = IsInitialBlockDownload();
    if(fIsInitialDownload) {
        status = MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasternode::ManageStatus() - Sync in progress. Must wait until sync is complete to start masternode.\n");
        return;
    }

    if(status == MASTERNODE_INPUT_TOO_NEW || status == MASTERNODE_NOT_CAPABLE || status == MASTERNODE_SYNC_IN_PROCESS){
        status = MASTERNODE_NOT_PROCESSED;
    }

    if(status == MASTERNODE_NOT_PROCESSED) {
        if(strMasterNodeAddr.empty()) {
            if(!GetLocal(service)) {
                notCapableReason = "Can't detect external address. Please use the masternodeaddr configuration option.";
                status = MASTERNODE_NOT_CAPABLE;
                LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
                return;
            }
        } else {
        	service = CService(strMasterNodeAddr, true);
        }

        LogPrintf("CActiveMasternode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString().c_str());


            if(!ConnectNode((CAddress)service, service.ToString().c_str())){
                notCapableReason = "Perhaps using port=<coin-port> is needed, Could not connect to " + service.ToString();
                status = MASTERNODE_NOT_CAPABLE;
                LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
                return;
            }


        if(pwalletMain->IsLocked()){
            notCapableReason = "Wallet is locked.";
            status = MASTERNODE_NOT_CAPABLE;
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason.c_str());
            return;
        }

        // Set defaults
        status = MASTERNODE_NOT_CAPABLE;
        notCapableReason = "Unknown. Check debug.log for more information.\n";

        // Choose coins to use
        CPubKey pubKeyCollateralAddress;
        CKey keyCollateralAddress;

        if(GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress)) {

            if(GetInputAge(vin) < MASTERNODE_MIN_CONFIRMATIONS){
                notCapableReason = "Input must have least " + boost::lexical_cast<string>(MASTERNODE_MIN_CONFIRMATIONS) +
                        " confirmations - " + boost::lexical_cast<string>(GetInputAge(vin)) + " confirmations";
                LogPrintf("CActiveMasternode::ManageStatus() - %s\n", notCapableReason.c_str());
                status = MASTERNODE_INPUT_TOO_NEW;
                return;
            }

            LogPrintf("CActiveMasternode::ManageStatus() - Is capable master node!\n");

            status = MASTERNODE_IS_CAPABLE;
            notCapableReason = "";

            pwalletMain->LockCoin(vin.prevout);

            // send to all nodes
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            if(!darkSendSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode))
            {
            	LogPrintf("ActiveMasternode::Dseep() - Error upon calling SetKey: %s\n", errorMessage.c_str());
            	return;
            }

            /* rewards are not supported in Anarcoin.conf */
            CScript rewardAddress = CScript();
            int rewardPercentage = 0;

            if(!Register(vin, service, keyCollateralAddress, pubKeyCollateralAddress, keyMasternode, pubKeyMasternode, rewardAddress, rewardPercentage, errorMessage)) {
                LogPrintf("CActiveMasternode::ManageStatus() - Error on Register: %s\n", errorMessage.c_str());
            }

            return;
        } else {
            notCapableReason = "Could not find suitable coins!";
        	LogPrintf("CActiveMasternode::ManageStatus() - Could not find suitable coins!\n");
        }
    }

    //send to all peers
    if(!Dseep(errorMessage)) {
    	LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage.c_str());
    }
}

// Send stop dseep to network for remote masternode
bool CActiveMasternode::StopMasterNode(std::string strService, std::string strKeyMasternode, std::string& errorMessage) {
	CTxIn vin;
    CKey keyMasternode;
    CPubKey pubKeyMasternode;

    if(!darkSendSigner.SetKey(strKeyMasternode, errorMessage, keyMasternode, pubKeyMasternode)) {
    	LogPrintf("CActiveMasternode::StopMasterNode() - Error: %s\n", errorMessage.c_str());
		return false;
	}

    if (GetMasterNodeVin(vin, pubKeyMasternode, keyMasternode)){
        LogPrintf("MasternodeStop::VinFound: %s\n", vin.ToString());
    }

	return StopMasterNode(vin, CService(strService, true), keyMasternode, pubKeyMasternode, errorMessage);
}

// Send stop dseep to network for main masternode
bool CActiveMasternode::StopMasterNode(std::string& errorMessage) {
	if(status != MASTERNODE_IS_CAPABLE && status != MASTERNODE_REMOTELY_ENABLED) {
		errorMessage = "masternode is not in a running status";
    	LogPrintf("CActiveMasternode::StopMasterNode() - Error: %s\n", errorMessage.c_str());
		return false;
	}

	status = MASTERNODE_STOPPED;

    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if(!darkSendSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode))
    {
    	LogPrintf("Register::ManageStatus() - Error upon calling SetKey: %s\n", errorMessage.c_str());
    	return false;
    }

	return StopMasterNode(vin, service, keyMasternode, pubKeyMasternode, errorMessage);
}

// Send stop dseep to network for any masternode
bool CActiveMasternode::StopMasterNode(CTxIn vin, CService service, CKey keyMasternode, CPubKey pubKeyMasternode, std::string& errorMessage) {
   	pwalletMain->UnlockCoin(vin.prevout);
	return Dseep(vin, service, keyMasternode, pubKeyMasternode, errorMessage, true);
}

bool CActiveMasternode::Dseep(std::string& errorMessage) {
	if(status != MASTERNODE_IS_CAPABLE && status != MASTERNODE_REMOTELY_ENABLED) {
		errorMessage = "masternode is not in a running status";
    	LogPrintf("CActiveMasternode::Dseep() - Error: %s\n", errorMessage.c_str());
		return false;
	}

    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if(!darkSendSigner.SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode))
    {
    	LogPrintf("CActiveMasternode::Dseep() - Error upon calling SetKey: %s\n", errorMessage.c_str());
    	return false;
    }

	return Dseep(vin, service, keyMasternode, pubKeyMasternode, errorMessage, false);
}

bool CActiveMasternode::Dseep(CTxIn vin, CService service, CKey keyMasternode, CPubKey pubKeyMasternode, std::string &retErrorMessage, bool stop) {
    std::string errorMessage;
    std::vector<unsigned char> vchMasterNodeSignature;
    std::string strMasterNodeSignMessage;
    int64_t masterNodeSignatureTime = GetAdjustedTime();

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(masterNodeSignatureTime) + boost::lexical_cast<std::string>(stop);

    if(!darkSendSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, keyMasternode)) {
    	retErrorMessage = "sign message failed: " + errorMessage;
    	LogPrintf("CActiveMasternode::Dseep() - Error: %s\n", retErrorMessage.c_str());
        return false;
    }

    if(!darkSendSigner.VerifyMessage(pubKeyMasternode, vchMasterNodeSignature, strMessage, errorMessage)) {
    	retErrorMessage = "Verify message failed: " + errorMessage;
    	LogPrintf("CActiveMasternode::Dseep() - Error: %s\n", retErrorMessage.c_str());
        return false;
    }

    // Update Last Seen timestamp in masternode list
    CMasternode* pmn = mnodeman.Find(vin);
    if(pmn != NULL)
    {
        if(stop)
            mnodeman.Remove(pmn->vin);
        else
            pmn->UpdateLastSeen();
    } else {
    	// Seems like we are trying to send a ping while the masternode is not registered in the network
    	retErrorMessage = "Darksend Masternode List doesn't include our masternode, Shutting down masternode pinging service! " + vin.ToString();
    	LogPrintf("CActiveMasternode::Dseep() - Error: %s\n", retErrorMessage.c_str());
        status = MASTERNODE_NOT_CAPABLE;
        notCapableReason = retErrorMessage;
        return false;
    }

    //send to all peers
    LogPrintf("CActiveMasternode::Dseep() - RelayMasternodeEntryPing vin = %s\n", vin.ToString().c_str());
    mnodeman.RelayMasternodeEntryPing(vin, vchMasterNodeSignature, masterNodeSignatureTime, stop);

    return true;
}

bool CActiveMasternode::Register(std::string strService, std::string strKeyMasternode, std::string txHash, std::string strOutputIndex, std::string strRewardAddress, std::string strRewardPercentage, std::string& errorMessage) {
    CTxIn vin;
    CPubKey pubKeyCollateralAddress;
    CKey keyCollateralAddress;
    CPubKey pubKeyMasternode;
    CKey keyMasternode;
    CScript rewardAddress = CScript();
    int rewardPercentage = 0;

    if(!darkSendSigner.SetKey(strKeyMasternode, errorMessage, keyMasternode, pubKeyMasternode))
    {
        LogPrintf("CActiveMasternode::Register() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    if(!GetMasterNodeVin(vin, pubKeyCollateralAddress, keyCollateralAddress, txHash, strOutputIndex)) {
        errorMessage = "could not allocate vin";
        LogPrintf("CActiveMasternode::Register() - Error: %s\n", errorMessage.c_str());
        return false;
    }
    CAnarcoincoinAddress address;
    if (strRewardAddress != "")
    {
        if(!address.SetString(strRewardAddress))
        {
            LogPrintf("ActiveMasternode::Register - Invalid Reward Address\n");
            return false;
        }
        rewardAddress.SetDestination(address.Get());

        try {
            rewardPercentage = boost::lexical_cast<int>( strRewardPercentage );
        } catch( boost::bad_lexical_cast const& ) {
            LogPrintf("ActiveMasternode::Register - Invalid Reward Percentage (Couldn't cast)\n");
            return false;
        }

        if(rewardPercentage < 0 || rewardPercentage > 100)
        {
            LogPrintf("ActiveMasternode::Register - Reward Percentage Out Of Range\n");
            return false;
        }
    }

	return Register(vin, CService(strService, true), keyCollateralAddress, pubKeyCollateralAddress, keyMasternode, pubKeyMasternode, rewardAddress, rewardPercentage, errorMessage);
}

bool CActiveMasternode::Register(CTxIn vin, CService service, CKey keyCollateralAddress, CPubKey pubKeyCollateralAddress, CKey keyMasternode, CPubKey pubKeyMasternode, CScript rewardAddress, int rewardPercentage, std::string &retErrorMessage) {
    std::string errorMessage;
    std::vector<unsigned char> vchMasterNodeSignature;
    std::string strMasterNodeSignMessage;
    int64_t masterNodeSignatureTime = GetAdjustedTime();

    std::string vchPubKey(pubKeyCollateralAddress.begin(), pubKeyCollateralAddress.end());
    std::string vchPubKey2(pubKeyMasternode.begin(), pubKeyMasternode.end());

    std::string strMessage = service.ToString() + boost::lexical_cast<std::string>(masterNodeSignatureTime) + vchPubKey + vchPubKey2 + boost::lexical_cast<std::string>(PROTOCOL_VERSION) + rewardAddress.ToString() + boost::lexical_cast<std::string>(rewardPercentage);

    if(!darkSendSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, keyCollateralAddress)) {
		retErrorMessage = "sign message failed: " + errorMessage;
		LogPrintf("CActiveMasternode::Register() - Error: %s\n", retErrorMessage.c_str());
		return false;
    }

    if(!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchMasterNodeSignature, strMessage, errorMessage)) {
		retErrorMessage = "Verify message failed: " + errorMessage;
		LogPrintf("CActiveMasternode::Register() - Error: %s\n", retErrorMessage.c_str());
		return false;
	}

    CMasternode* pmn = mnodeman.Find(vin);
    if(pmn == NULL)
    {
        LogPrintf("CActiveMasternode::Register() - Adding to masternode list service: %s - vin: %s\n", service.ToString().c_str(), vin.ToString().c_str());
        CMasternode mn(service, vin, pubKeyCollateralAddress, vchMasterNodeSignature, masterNodeSignatureTime, pubKeyMasternode, PROTOCOL_VERSION, rewardAddress, rewardPercentage);
        mn.ChangeNodeStatus(false);
        mn.UpdateLastSeen(masterNodeSignatureTime);
        mnodeman.Add(mn);
    }

    //send to all peers
    LogPrintf("CActiveMasternode::Register() - RelayElectionEntry vin = %s\n", vin.ToString().c_str());
    mnodeman.RelayMasternodeEntry(vin, service, vchMasterNodeSignature, masterNodeSignatureTime, pubKeyCollateralAddress, pubKeyMasternode, -1, -1, masterNodeSignatureTime, PROTOCOL_VERSION, rewardAddress, rewardPercentage);

    return true;
}

bool CActiveMasternode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
	return GetMasterNodeVin(vin, pubkey, secretKey, "", "");
}

bool CActiveMasternode::GetMasterNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    CScript pubScript;

    // Find possible candidates
    vector<COutput> possibleCoins = SelectCoinsMasternode();
    COutput *selectedOutput;

    // Find the vin
	if(!strTxHash.empty()) {
		// Let's find it
		uint256 txHash(strTxHash);
        int outputIndex = boost::lexical_cast<int>(strOutputIndex);
		bool found = false;
		BOOST_FOREACH(COutput& out, possibleCoins) {
			if(out.tx->GetHash() == txHash && out.i == outputIndex)
			{
				selectedOutput = &out;
				found = true;
				break;
			}
		}
		if(!found) {
			LogPrintf("CActiveMasternode::GetMasterNodeVin - Could not locate valid vin\n");
			return false;
		}
	} else {
		// No output specified,  Select the first one
		if(possibleCoins.size() > 0) {
			selectedOutput = &possibleCoins[0];
		} else {
			LogPrintf("CActiveMasternode::GetMasterNodeVin - Could not locate specified vin from possible list\n");
			return false;
		}
    }

	// At this point we have a selected output, retrieve the associated info
	return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}

bool CActiveMasternode::GetMasterNodeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {
	return GetMasterNodeVinForPubKey(collateralAddress, vin, pubkey, secretKey, "", "");
}

bool CActiveMasternode::GetMasterNodeVinForPubKey(std::string collateralAddress, CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex) {
    CScript pubScript;

    // Find possible candidates
    vector<COutput> possibleCoins = SelectCoinsMasternodeForPubKey(collateralAddress);
    COutput *selectedOutput;

    // Find the vin
	if(!strTxHash.empty()) {
		// Let's find it
		uint256 txHash(strTxHash);
        int outputIndex = boost::lexical_cast<int>(strOutputIndex);
		bool found = false;
		BOOST_FOREACH(COutput& out, possibleCoins) {
			if(out.tx->GetHash() == txHash && out.i == outputIndex)
			{
				selectedOutput = &out;
				found = true;
				break;
			}
		}
		if(!found) {
			LogPrintf("CActiveMasternode::GetMasterNodeVinForPubKey - Could not locate valid vin\n");
			return false;
		}
	} else {
		// No output specified,  Select the first one
		if(possibleCoins.size() > 0) {
			selectedOutput = &possibleCoins[0];
		} else {
			LogPrintf("CActiveMasternode::GetMasterNodeVinForPubKey - Could not locate specified vin from possible list\n");
			return false;
		}
    }

	// At this point we have a selected output, retrieve the associated info
	return GetVinFromOutput(*selectedOutput, vin, pubkey, secretKey);
}


// Extract masternode vin information from output
bool CActiveMasternode::GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey) {

    CScript pubScript;

	vin = CTxIn(out.tx->GetHash(),out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

	CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CAnarcoincoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("CActiveMasternode::GetMasterNodeVin - Address does not refer to a key\n");
        return false;
    }

    if (!pwalletMain->GetKey(keyID, secretKey)) {
        LogPrintf ("CActiveMasternode::GetMasterNodeVin - Private key for address is not known\n");
        return false;
    }

    pubkey = secretKey.GetPubKey();
    return true;
}

// get all possible outputs for running masternode
vector<COutput> CActiveMasternode::SelectCoinsMasternode()
{
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;

    // Retrieve all possible outputs
    pwalletMain->AvailableCoinsMN(vCoins);

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].nValue == GetMNCollateral(pindexBest->nHeight)*COIN) { //exactly
        	filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// get all possible outputs for running masternode for a specific pubkey
vector<COutput> CActiveMasternode::SelectCoinsMasternodeForPubKey(std::string collateralAddress)
{
    CAnarcoincoinAddress address(collateralAddress);
    CScript scriptPubKey;
    scriptPubKey.SetDestination(address.Get());
    vector<COutput> vCoins;
    vector<COutput> filteredCoins;

    // Retrieve all possible outputs
    pwalletMain->AvailableCoins(vCoins);

    // Filter
    BOOST_FOREACH(const COutput& out, vCoins)
    {
        if(out.tx->vout[out.i].scriptPubKey == scriptPubKey && out.tx->vout[out.i].nValue == GetMNCollateral(pindexBest->nHeight)*COIN) { //exactly
        	filteredCoins.push_back(out);
        }
    }
    return filteredCoins;
}

// when starting a masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if(!fMasterNode) return false;

    status = MASTERNODE_REMOTELY_ENABLED;

    //The values below are needed for signing dseep messages going forward
    this->vin = newVin;
    this->service = newService;

    LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}
