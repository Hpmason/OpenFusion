#include "ChunkManager.hpp"
#include "PlayerManager.hpp"
#include "NPCManager.hpp"
#include "settings.hpp"

std::map<std::tuple<int, int, uint64_t>, Chunk*> ChunkManager::chunks;

void ChunkManager::init() {} // stubbed

void ChunkManager::addNPC(int posX, int posY, uint64_t instanceID, int32_t id) {
    std::tuple<int, int, uint64_t> pos = grabChunk(posX, posY, instanceID);

    // make chunk if it doesn't exist!
    if (chunks.find(pos) == chunks.end()) {
        chunks[pos] = new Chunk();
        chunks[pos]->players = std::set<CNSocket*>();
        chunks[pos]->NPCs = std::set<int32_t>();
    }

    Chunk* chunk = chunks[pos];

    chunk->NPCs.insert(id);
}

void ChunkManager::addPlayer(int posX, int posY, uint64_t instanceID, CNSocket* sock) {
    std::tuple<int, int, uint64_t> pos = grabChunk(posX, posY, instanceID);

    // make chunk if it doesn't exist!
    if (chunks.find(pos) == chunks.end()) {
        chunks[pos] = new Chunk();
        chunks[pos]->players = std::set<CNSocket*>();
        chunks[pos]->NPCs = std::set<int32_t>();
    }

    Chunk* chunk = chunks[pos];

    chunk->players.insert(sock);
}

bool ChunkManager::removePlayer(std::tuple<int, int, uint64_t> chunkPos, CNSocket* sock) {
    if (!checkChunk(chunkPos))
        return false; // do nothing if chunk doesn't even exist

    Chunk* chunk = chunks[chunkPos];

    chunk->players.erase(sock); // gone

    // if players and NPCs are empty, free chunk and remove it from surrounding views
    if (chunk->NPCs.size() == 0 && chunk->players.size() == 0) {
        destroyChunk(chunkPos);

        // the chunk we left was destroyed
        return true;
    }

    // the chunk we left was not destroyed
    return false;
}

bool ChunkManager::removeNPC(std::tuple<int, int, uint64_t> chunkPos, int32_t id) {
    if (!checkChunk(chunkPos))
        return false; // do nothing if chunk doesn't even exist

    Chunk* chunk = chunks[chunkPos];

    chunk->NPCs.erase(id); // gone

    // if players and NPCs are empty, free chunk and remove it from surrounding views
    if (chunk->NPCs.size() == 0 && chunk->players.size() == 0) {
        destroyChunk(chunkPos);

        // the chunk we left was destroyed
        return true;
    }

    // the chunk we left was not destroyed
    return false;
}

void ChunkManager::destroyChunk(std::tuple<int, int, uint64_t> chunkPos) {
    if (!checkChunk(chunkPos))
        return; // chunk doesn't exist, we don't need to do anything

    Chunk* chunk = chunks[chunkPos];

    // unspawn all of the mobs/npcs
    for (uint32_t id : chunk->NPCs) {
        NPCManager::destroyNPC(id);
    }

    // we also need to remove it from all NPCs/Players views
    for (Chunk* otherChunk : grabChunks(chunkPos)) {
        if (otherChunk == chunk)
            continue;
        
        // remove from NPCs
        for (uint32_t id : otherChunk->NPCs) {
            if (std::find(NPCManager::NPCs[id]->currentChunks.begin(), NPCManager::NPCs[id]->currentChunks.end(), chunk) != NPCManager::NPCs[id]->currentChunks.end()) {
                NPCManager::NPCs[id]->currentChunks.erase(std::remove(NPCManager::NPCs[id]->currentChunks.begin(), NPCManager::NPCs[id]->currentChunks.end(), chunk), NPCManager::NPCs[id]->currentChunks.end());
            }
        }

        // remove from players
        for (CNSocket* sock : otherChunk->players) {
            PlayerView* plyr = &PlayerManager::players[sock];
            if (std::find(plyr->currentChunks.begin(), plyr->currentChunks.end(), chunk) != plyr->currentChunks.end()) {
                plyr->currentChunks.erase(std::remove(plyr->currentChunks.begin(), plyr->currentChunks.end(), chunk), plyr->currentChunks.end());
            }
        }
    }


    assert(chunk->players.size() == 0);

    // remove from the map
    chunks.erase(chunkPos);

    delete chunk;
}

bool ChunkManager::checkChunk(std::tuple<int, int, uint64_t> chunk) {
    return chunks.find(chunk) != chunks.end();
}

std::tuple<int, int, uint64_t> ChunkManager::grabChunk(int posX, int posY, uint64_t instanceID) {
    return std::make_tuple(posX / (settings::VIEWDISTANCE / 3), posY / (settings::VIEWDISTANCE / 3), instanceID);
}

std::vector<Chunk*> ChunkManager::grabChunks(std::tuple<int, int, uint64_t> chunk) {
    std::vector<Chunk*> chnks;
    chnks.reserve(9);

    int x, y;
    uint64_t inst;
    std::tie(x, y, inst) = chunk;

    // grabs surrounding chunks if they exist
    for (int i = -1; i < 2; i++) {
        for (int z = -1; z < 2; z++) {
            std::tuple<int, int, uint64_t> pos = std::make_tuple(x+i, y+z, inst);

            // if chunk exists, add it to the vector
            if (checkChunk(pos))
                chnks.push_back(chunks[pos]);
        }
    }

    return chnks;
}

// returns the chunks that aren't shared (only from from)
std::vector<Chunk*> ChunkManager::getDeltaChunks(std::vector<Chunk*> from, std::vector<Chunk*> to) {
    std::vector<Chunk*> delta;

    for (Chunk* i : from) {
        bool found = false;

        // search for it in the other array
        for (Chunk* z : to) {
            if (i == z) {
                found = true;
                break;
            }
        }

        // add it to the vector if we didn't find it!
        if (!found)
            delta.push_back(i);
    }

    return delta;
}

bool ChunkManager::inPopulatedChunks(int posX, int posY, uint64_t instanceID) {
    auto chunk = ChunkManager::grabChunk(posX, posY, instanceID);
    auto nearbyChunks = ChunkManager::grabChunks(chunk);

    for (Chunk *c: nearbyChunks) {
        if (!c->players.empty())
            return true;
    }

    return false;
}
