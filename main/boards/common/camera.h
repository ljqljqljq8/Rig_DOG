#ifndef CAMERA_H
#define CAMERA_H

#include <string>

class Camera {
public:
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual void SetFaceTrackUrl(const std::string& url) {
        (void)url;
    }
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual std::string Explain(const std::string& question) = 0;
    virtual std::string EnrollPerson(const std::string& name) {
        (void)name;
        return "<enroll>Face enrollment not supported</enroll>";
    }
    virtual std::string LocatePerson(const std::string& name) {
        (void)name;
        return "{\"success\":false,\"error\":\"Face locate not supported\"}";
    }
};

#endif // CAMERA_H
