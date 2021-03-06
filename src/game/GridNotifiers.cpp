/*
 * Copyright (C) 2010 DiamondCore <http://easy-emu.de/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "GridNotifiers.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "UpdateData.h"
#include "Item.h"
#include "Map.h"
#include "Transports.h"
#include "ObjectAccessor.h"

using namespace Diamond;

void
VisibleChangesNotifier::Visit(CameraMapType &m)
{
    for(CameraMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        iter->getSource()->UpdateVisibilityOf(&i_object);
    }
}

void
VisibleNotifier::Notify()
{
    Player& i_player = *camera.getOwner();
    // at this moment i_clientGUIDs have guids that not iterate at grid level checks
    // but exist one case when this possible and object not out of range: transports
    if(Transport* transport = i_player.GetTransport())
    {
        for(Transport::PlayerSet::const_iterator itr = transport->GetPassengers().begin();itr!=transport->GetPassengers().end();++itr)
        {
            if(i_clientGUIDs.find((*itr)->GetGUID())!=i_clientGUIDs.end())
            {
                // ignore far sight case
                (*itr)->UpdateVisibilityOf((*itr),&i_player);
                i_player.UpdateVisibilityOf(&i_player,(*itr),i_data,i_visibleNow);
                i_clientGUIDs.erase((*itr)->GetGUID());
            }
        }
    }

    // generate outOfRange for not iterate objects
    i_data.AddOutOfRangeGUID(i_clientGUIDs);
    for(ObjectGuidSet::iterator itr = i_clientGUIDs.begin();itr!=i_clientGUIDs.end();++itr)
    {
        i_player.m_clientGUIDs.erase(*itr);

        DEBUG_FILTER_LOG(LOG_FILTER_VISIBILITY_CHANGES, "%s is out of range (no in active cells set) now for player %u",itr->GetString().c_str(),i_player.GetGUIDLow());
    }

    if( i_data.HasData() )
    {
        // send create/outofrange packet to player (except player create updates that already sent using SendUpdateToPlayer)
        WorldPacket packet;
        i_data.BuildPacket(&packet);
        i_player.GetSession()->SendPacket(&packet);

        // send out of range to other players if need
        ObjectGuidSet const& oor = i_data.GetOutOfRangeGUIDs();
        for(ObjectGuidSet::const_iterator iter = oor.begin(); iter != oor.end(); ++iter)
        {
            if(!iter->IsPlayer())
                continue;

            if (Player* plr = ObjectAccessor::FindPlayer(*iter))
                plr->UpdateVisibilityOf(plr->GetCamera().getBody(),&i_player);
        }
    }

    // Now do operations that required done at object visibility change to visible

    // send data at target visibility change (adding to client)
    for(std::set<WorldObject*>::const_iterator vItr = i_visibleNow.begin(); vItr != i_visibleNow.end(); ++vItr)
    {
        // target aura duration for caster show only if target exist at caster client
        if ((*vItr) != &i_player && (*vItr)->isType(TYPEMASK_UNIT))
        {
            i_player.SendAurasForTarget((Unit*)(*vItr));
            WorldPacket data;
            ((Unit*)(*vItr))->BuildHeartBeatMsg(&data);
            i_player.GetSession()->SendPacket(&data);
        }

        // non finished movements show to player
        if((*vItr)->GetTypeId()==TYPEID_UNIT && ((Creature*)(*vItr))->isAlive())
            ((Creature*)(*vItr))->SendMonsterMoveWithSpeedToCurrentDestination(&i_player);
    }
}

void
MessageDeliverer::Visit(CameraMapType &m)
{
    for(CameraMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        if (i_toSelf || iter->getSource()->getOwner() != &i_player)
        {
            if (!i_player.InSamePhase(iter->getSource()->getBody()))
                continue;

            if(WorldSession* session = iter->getSource()->getOwner()->GetSession())
                session->SendPacket(i_message);
        }
    }
}

void MessageDelivererExcept::Visit(CameraMapType &m)
{
    for(CameraMapType::iterator it = m.begin(); it!= m.end(); ++it)
    {
        Player* player = it->getSource()->getOwner();
        if(!player->InSamePhase(i_phaseMask) || player == i_skipped_receiver)
            continue;

        if (WorldSession* session = player->GetSession())
            session->SendPacket(i_message);
    }
}


void
ObjectMessageDeliverer::Visit(CameraMapType &m)
{
    for(CameraMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        if(!iter->getSource()->getBody()->InSamePhase(i_phaseMask))
            continue;

        if(WorldSession* session = iter->getSource()->getOwner()->GetSession())
            session->SendPacket(i_message);
    }
}

void
MessageDistDeliverer::Visit(CameraMapType &m)
{
    for(CameraMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        WorldObject* body = iter->getSource()->getBody();
        Player * owner = iter->getSource()->getOwner();
        if ((i_toSelf || owner != &i_player ) &&
            (!i_ownTeamOnly || owner->GetTeam() == i_player.GetTeam() ) &&
            (!i_dist || body->IsWithinDist(&i_player,i_dist)))
        {
            if (!i_player.InSamePhase(body))
                continue;

            if (WorldSession* session = owner->GetSession())
                session->SendPacket(i_message);
        }
    }
}

void
ObjectMessageDistDeliverer::Visit(CameraMapType &m)
{
    for(CameraMapType::iterator iter=m.begin(); iter != m.end(); ++iter)
    {
        if (!i_dist || iter->getSource()->getBody()->IsWithinDist(&i_object,i_dist))
        {
            if (!i_object.InSamePhase(iter->getSource()->getBody()))
                continue;

            if (WorldSession* session = iter->getSource()->getOwner()->GetSession())
                session->SendPacket(i_message);
        }
    }
}

template<class T> void
ObjectUpdater::Visit(GridRefManager<T> &m)
{
    for(typename GridRefManager<T>::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        iter->getSource()->Update(i_timeDiff);
    }
}

bool CannibalizeObjectCheck::operator()(Corpse* u)
{
    // ignore bones
    if(u->GetType()==CORPSE_BONES)
        return false;

    Player* owner = ObjectAccessor::FindPlayer(u->GetOwnerGUID());

    if( !owner || i_fobj->IsFriendlyTo(owner))
        return false;

    if(i_fobj->IsWithinDistInMap(u, i_range) )
        return true;

    return false;
}

template void ObjectUpdater::Visit<GameObject>(GameObjectMapType &);
template void ObjectUpdater::Visit<DynamicObject>(DynamicObjectMapType &);
