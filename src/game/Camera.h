
#ifndef _CAMERA_H
#define _CAMERA_H

#include "GridDefines.h"

class ViewPoint;
class WorldObject;
class UpdateData;
class WorldPacket;

/// Camera - object-receiver. Receives broadcast packets from nearby worldobjects, object visibility changes and sends them to client
class Camera
{
    friend class ViewPoint;
    public:

        Camera(Player* pl);
        ~Camera();

        WorldObject* getBody() { return m_source;}
        Player* getOwner() { return &m_owner;}

        // set camera's view to any worldobject
        // Note: this worldobject must be in same map, in same phase with camera's owner(player)
        // client supports only unit and dynamic objects as farsight objects
        void SetView(WorldObject *obj);

        // set view to camera's owner
        void ResetView();

        template<class T>
        void UpdateVisibilityOf(T * obj, UpdateData &d, std::set<WorldObject*>& vis);
        void UpdateVisibilityOf(WorldObject* obj);

        void ReceivePacket(WorldPacket *data);
        void UpdateVisibilityForOwner();    // updates visibility of worldobjects around viewpoint for camera's owner

    private:
        // called when viewpoint changes visibility state
        void Event_AddedToWorld();
        void Event_RemovedFromWorld();
        void Event_Moved();
        void Event_ViewPointVisibilityChanged();

        Player & m_owner;
        WorldObject *m_source;

        void UpdateForCurrentViewPoint();

    public:
        GridReference<Camera>& GetGridRef() { return m_gridRef; }
        bool isActiveObject() const { return false; }
    private:
        GridReference<Camera> m_gridRef;
};

/// Object-observer, notifies farsight object state to cameras that attached to it
class ViewPoint
{
    friend class Camera;

    std::list<Camera*> m_cameras;
    std::list<Camera*>::iterator camera_iter;
    GridType * m_grid;
    
    void Attach(Camera* c) { m_cameras.push_back(c); }

    void Detach(Camera* c)
    {
        if (camera_iter != m_cameras.end() && *camera_iter == c)     // detach called during the loop
            camera_iter = m_cameras.erase(camera_iter);
        else
            m_cameras.remove(c);
    }

    void CameraCall(void (Camera::*handler)())
    {
        if (!m_cameras.empty())
        {
            for(camera_iter = m_cameras.begin(); camera_iter != m_cameras.end(); ++camera_iter)
            {
                ((*camera_iter)->*handler)();

                // can be end() after handler
                if (camera_iter == m_cameras.end())
                    break;
            }
        }
    }

public:

    ViewPoint() : m_grid(0), camera_iter(m_cameras.end()) {}
    ~ViewPoint();

    bool hasViewers() const { return !m_cameras.empty(); }

    // these events are called when viewpoint changes visibility state
    void Event_AddedToWorld(GridType *grid)
    {
        m_grid = grid;
        CameraCall(&Camera::Event_AddedToWorld);
    }

    void Event_RemovedFromWorld()
    {
        m_grid = NULL;
        CameraCall(&Camera::Event_RemovedFromWorld);
    }

    void Event_GridChanged(GridType *grid)
    {
        m_grid = grid;
        CameraCall(&Camera::Event_Moved);
    }

    void Event_ViewPointVisibilityChanged()
    {
        CameraCall(&Camera::Event_ViewPointVisibilityChanged);
    }

    void Call_UpdateVisibilityForOwner()
    {
        CameraCall(&Camera::UpdateVisibilityForOwner);
    }
};



#endif