#include "CNShardServer.hpp"
#include "CNStructs.hpp"
#include "PlayerManager.hpp"
#include "TransportManager.hpp"

#include <unordered_map>

std::map<int32_t, TransportRoute> TransportManager::Routes;
std::map<int32_t, TransportLocation> TransportManager::Locations;
std::map<int32_t, std::queue<WarpLocation>> TransportManager::SkywayPaths;
std::unordered_map<CNSocket*, std::queue<WarpLocation>> TransportManager::SkywayQueue;

void TransportManager::init() {
    REGISTER_SHARD_TIMER(tickSkywaySystem, 1000);

    REGISTER_SHARD_PACKET(P_CL2FE_REQ_REGIST_TRANSPORTATION_LOCATION, transportRegisterLocationHandler);
    REGISTER_SHARD_PACKET(P_CL2FE_REQ_PC_WARP_USE_TRANSPORTATION, transportWarpHandler);
}

void TransportManager::transportRegisterLocationHandler(CNSocket* sock, CNPacketData* data) {
    if (data->size != sizeof(sP_CL2FE_REQ_REGIST_TRANSPORTATION_LOCATION))
        return; // malformed packet

    sP_CL2FE_REQ_REGIST_TRANSPORTATION_LOCATION* transport = (sP_CL2FE_REQ_REGIST_TRANSPORTATION_LOCATION*)data->buf;
    Player* plr = PlayerManager::getPlayer(sock);

    //std::cout << "request to register transport, eTT " << transport->eTT << ", locID " << transport->iLocationID << ", npc " << transport->iNPC_ID << std::endl;
    if (transport->eTT == 1) { // S.C.A.M.P.E.R.
        if (transport->iLocationID < 1 || transport->iLocationID > 31) { // sanity check
            std::cout << "[WARN] S.C.A.M.P.E.R. location ID " << transport->iLocationID << " is out of bounds" << std::endl;
            INITSTRUCT(sP_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL, failResp);

            failResp.eTT = transport->eTT;
            failResp.iErrorCode = 0; // TODO: review what error code to use here
            failResp.iLocationID = transport->iLocationID;

            sock->sendPacket((void*)&failResp, P_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL, sizeof(sP_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL));
            return;
        }

        // update registration bitfield using bit shifting + bitwise or
        plr->iWarpLocationFlag |= plr->IsGM ? INT32_MAX : (1UL << (transport->iLocationID - 1));
    } else if (transport->eTT == 2) { // Monkey Skyway System
        if (transport->iLocationID < 1 || transport->iLocationID > 127) { // sanity check
            std::cout << "[WARN] Skyway location ID " << transport->iLocationID << " is out of bounds" << std::endl;
            INITSTRUCT(sP_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL, failResp);

            failResp.eTT = transport->eTT;
            failResp.iErrorCode = 0; // TODO: review what error code to use here
            failResp.iLocationID = transport->iLocationID;

            sock->sendPacket((void*)&failResp, P_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL, sizeof(sP_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL));
            return;
        }

        /*
         * assuming the two bitfields are just stuck together to make a longer one... do a similar operation, but on the respective integer
         * this approach seems to work with initial testing, but we have yet to see a monkey ID greater than 63.
         */
        if (plr->IsGM) {
            plr->aSkywayLocationFlag[0] = INT64_MAX;
            plr->aSkywayLocationFlag[1] = INT64_MAX;
        }
        else {
            plr->aSkywayLocationFlag[transport->iLocationID > 63 ? 1 : 0] |= (1ULL << (transport->iLocationID > 63 ? transport->iLocationID - 65 : transport->iLocationID - 1));
        }
    } else {
        std::cout << "[WARN] Unknown mode of transport; eTT = " << transport->eTT << std::endl;
        INITSTRUCT(sP_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL, failResp);

        failResp.eTT = transport->eTT;
        failResp.iErrorCode = 0; // TODO: review what error code to use here
        failResp.iLocationID = transport->iLocationID;

        sock->sendPacket((void*)&failResp, P_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL, sizeof(sP_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_FAIL));
        return;
    }

    INITSTRUCT(sP_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_SUCC, resp);

    // response parameters
    resp.eTT = transport->eTT;
    resp.iLocationID = transport->iLocationID;
    resp.iWarpLocationFlag = plr->iWarpLocationFlag;
    resp.aWyvernLocationFlag[0] = plr->aSkywayLocationFlag[0];
    resp.aWyvernLocationFlag[1] = plr->aSkywayLocationFlag[1];

    sock->sendPacket((void*)&resp, P_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_SUCC, sizeof(sP_FE2CL_REP_PC_REGIST_TRANSPORTATION_LOCATION_SUCC));
}

void TransportManager::transportWarpHandler(CNSocket* sock, CNPacketData* data) {
    if (data->size != sizeof(sP_CL2FE_REQ_PC_WARP_USE_TRANSPORTATION))
        return; // malformed packet

    sP_CL2FE_REQ_PC_WARP_USE_TRANSPORTATION* req = (sP_CL2FE_REQ_PC_WARP_USE_TRANSPORTATION*)data->buf;
    Player* plr = PlayerManager::getPlayer(sock);

    /*
     * req:
     * eIL -- inventory type
     * iNPC_ID -- the ID of the NPC who is warping you
     * iTransporationID -- iVehicleID
     * iSlotNum -- inventory slot number
     */

    if (Routes.find(req->iTransporationID) == Routes.end() || Routes[req->iTransporationID].cost > plr->money) { // sanity check
        INITSTRUCT(sP_FE2CL_REP_PC_WARP_USE_TRANSPORTATION_FAIL, failResp);

        failResp.iErrorCode = 0; // TODO: error code
        failResp.iTransportationID = req->iTransporationID;

        sock->sendPacket((void*)&failResp, P_FE2CL_REP_PC_WARP_USE_TRANSPORTATION_FAIL, sizeof(sP_FE2CL_REP_PC_WARP_USE_TRANSPORTATION_FAIL));
        return;
    }

    TransportRoute route = Routes[req->iTransporationID];
    plr->money -= route.cost;

    TransportLocation target;
    PlayerView& plrv = PlayerManager::players[sock];
    std::queue<WarpLocation>* points = nullptr;
    switch (route.type)
    {
    case 1: // S.C.A.M.P.E.R.
        target = Locations[route.end];
        plr->x = target.x;
        plr->y = target.y;
        plr->z = target.z;
        /*
         * Not strictly necessary since there isn't a valid SCAMPER that puts you in the
         * same map tile you were already in, but we might as well force an NPC reload.
         */
        PlayerManager::removePlayerFromChunks(plrv.currentChunks, sock);
        plrv.currentChunks.clear();
        plrv.chunkPos = std::make_pair<int, int>(0, 0);
        break;
    case 2: // Monkey Skyway
        if (SkywayPaths.find(route.mssRouteNum) != SkywayPaths.end()) // sanity check
            SkywayQueue[sock] = SkywayPaths[route.mssRouteNum];
        else
            std::cout << "[WARN] MSS route " << route.mssRouteNum << " not pathed" << std::endl;
        break;
    default:
        std::cout << "[WARN] Unknown tranportation type " << route.type << std::endl;
        break;
    }

    INITSTRUCT(sP_FE2CL_REP_PC_WARP_USE_TRANSPORTATION_SUCC, resp);
    // response parameters
    resp.eTT = route.type;
    resp.iCandy = plr->money;
    resp.iX = plr->x;
    resp.iY = plr->y;
    resp.iZ = plr->z;
    sock->sendPacket((void*)&resp, P_FE2CL_REP_PC_WARP_USE_TRANSPORTATION_SUCC, sizeof(sP_FE2CL_REP_PC_WARP_USE_TRANSPORTATION_SUCC));
}

void TransportManager::tickSkywaySystem(CNServer* serv, time_t currTime) {
    
    //std::cout << SkywayQueue.size();
    // using an unordered list so we can remove finished players in one iteration
    std::unordered_map<CNSocket*, std::queue<WarpLocation>>::iterator it = SkywayQueue.begin();
    while (it != SkywayQueue.end()) {

        std::queue<WarpLocation>* queue = &it->second;
        PlayerView& plr = PlayerManager::players[it->first];

        if (plr.plr == nullptr) {
            // pluck out dead queue + update iterator
            it = SkywayQueue.erase(it);
            continue;
        }

        if (queue->empty()) {
            // send dismount packet
            INITSTRUCT(sP_FE2CL_REP_PC_RIDING_SUCC, rideSucc);
            INITSTRUCT(sP_FE2CL_PC_RIDING, rideBroadcast);
            rideSucc.iPC_ID = plr.plr->iID;
            rideSucc.eRT = 0;
            rideBroadcast.iPC_ID = plr.plr->iID;
            rideBroadcast.eRT = 0;
            it->first->sendPacket((void*)&rideSucc, P_FE2CL_REP_PC_RIDING_SUCC, sizeof(sP_FE2CL_REP_PC_RIDING_SUCC));
            // send packet to players in view (the client does NOT like this for some reason)
            for (CNSocket* otherSock : plr.viewable)
                otherSock->sendPacket((void*)&rideBroadcast, P_FE2CL_PC_RIDING, sizeof(sP_FE2CL_PC_RIDING));
            it = SkywayQueue.erase(it); // remove player from tracking map + update iterator
        }
        else {
            WarpLocation point = queue->front(); // get point
            queue->pop(); // remove point from front of queue

            INITSTRUCT(sP_FE2CL_PC_BROOMSTICK_MOVE, bmstk);
            bmstk.iPC_ID = plr.plr->iID;
            bmstk.iToX = point.x;
            bmstk.iToY = point.y;
            bmstk.iToZ = point.z;
            it->first->sendPacket((void*)&bmstk, P_FE2CL_PC_BROOMSTICK_MOVE, sizeof(sP_FE2CL_PC_BROOMSTICK_MOVE));
            // set player location to point to get better view
            PlayerManager::updatePlayerPosition(it->first, point.x, point.y, point.z);
            // send packet to players in view
            for(CNSocket* otherSock : plr.viewable)
                otherSock->sendPacket((void*)&bmstk, P_FE2CL_PC_BROOMSTICK_MOVE, sizeof(sP_FE2CL_PC_BROOMSTICK_MOVE));

            it++; // go to next entry in map
        }
    }
}
