/**
 * Drive
 * Manages files in Drive including uploading, downloading, searching, detecting changes, and updating sharing permissions.
 *
 * OpenAPI spec version: v3
 * 
 *
 * NOTE: This class is auto generated by the swagger code generator 2.2.3.
 * https://github.com/swagger-api/swagger-codegen.git
 * Do not edit the class manually.
 */



#include "Channel.h"

namespace io {
namespace swagger {
namespace client {
namespace model {

Channel::Channel()
{
    m_Address = U("");
    m_AddressIsSet = false;
    m_Expiration = U("");
    m_ExpirationIsSet = false;
    m_Id = U("");
    m_IdIsSet = false;
    m_Kind = U("");
    m_KindIsSet = false;
    m_ParamsIsSet = false;
    m_Payload = false;
    m_PayloadIsSet = false;
    m_ResourceId = U("");
    m_ResourceIdIsSet = false;
    m_ResourceUri = U("");
    m_ResourceUriIsSet = false;
    m_Token = U("");
    m_TokenIsSet = false;
    m_Type = U("");
    m_TypeIsSet = false;
}

Channel::~Channel()
{
}

void Channel::validate()
{
    // TODO: implement validation
}

web::json::value Channel::toJson() const
{
    web::json::value val = web::json::value::object();

    if(m_AddressIsSet)
    {
        val[U("address")] = ModelBase::toJson(m_Address);
    }
    if(m_ExpirationIsSet)
    {
        val[U("expiration")] = ModelBase::toJson(m_Expiration);
    }
    if(m_IdIsSet)
    {
        val[U("id")] = ModelBase::toJson(m_Id);
    }
    if(m_KindIsSet)
    {
        val[U("kind")] = ModelBase::toJson(m_Kind);
    }
    {
        std::vector<web::json::value> jsonArray;
        for( auto& item : m_Params )
        {
            web::json::value tmp = web::json::value::object();
            tmp[U("key")] = ModelBase::toJson(item.first);
            tmp[U("value")] = ModelBase::toJson(item.second);
            jsonArray.push_back(tmp);
        }
        if(jsonArray.size() > 0)
        {
            val[U("params")] = web::json::value::array(jsonArray);
        }
    }
    if(m_PayloadIsSet)
    {
        val[U("payload")] = ModelBase::toJson(m_Payload);
    }
    if(m_ResourceIdIsSet)
    {
        val[U("resourceId")] = ModelBase::toJson(m_ResourceId);
    }
    if(m_ResourceUriIsSet)
    {
        val[U("resourceUri")] = ModelBase::toJson(m_ResourceUri);
    }
    if(m_TokenIsSet)
    {
        val[U("token")] = ModelBase::toJson(m_Token);
    }
    if(m_TypeIsSet)
    {
        val[U("type")] = ModelBase::toJson(m_Type);
    }

    return val;
}

void Channel::fromJson(web::json::value& val)
{
    if(val.has_field(U("address")))
    {
        setAddress(ModelBase::stringFromJson(val[U("address")]));
    }
    if(val.has_field(U("expiration")))
    {
        setExpiration(ModelBase::stringFromJson(val[U("expiration")]));
    }
    if(val.has_field(U("id")))
    {
        setId(ModelBase::stringFromJson(val[U("id")]));
    }
    if(val.has_field(U("kind")))
    {
        setKind(ModelBase::stringFromJson(val[U("kind")]));
    }
    {
        m_Params.clear();
        std::vector<web::json::value> jsonArray;
        if(val.has_field(U("params")))
        {
        for( auto& item : val[U("params")].as_array() )
        {  
            utility::string_t key;
            if(item.has_field(U("key")))
            {
                key = ModelBase::stringFromJson(item[U("key")]);
            }
            m_Params.insert(std::pair<utility::string_t,utility::string_t>( key, ModelBase::stringFromJson(item[U("value")])));
        }
        }
    }
    if(val.has_field(U("payload")))
    {
        setPayload(ModelBase::boolFromJson(val[U("payload")]));
    }
    if(val.has_field(U("resourceId")))
    {
        setResourceId(ModelBase::stringFromJson(val[U("resourceId")]));
    }
    if(val.has_field(U("resourceUri")))
    {
        setResourceUri(ModelBase::stringFromJson(val[U("resourceUri")]));
    }
    if(val.has_field(U("token")))
    {
        setToken(ModelBase::stringFromJson(val[U("token")]));
    }
    if(val.has_field(U("type")))
    {
        setType(ModelBase::stringFromJson(val[U("type")]));
    }
}

void Channel::toMultipart(std::shared_ptr<MultipartFormData> multipart, const utility::string_t& prefix) const
{
    utility::string_t namePrefix = prefix;
    if(namePrefix.size() > 0 && namePrefix[namePrefix.size() - 1] != U('.'))
    {
        namePrefix += U(".");
    }

    if(m_AddressIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("address"), m_Address));
        
    }
    if(m_ExpirationIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("expiration"), m_Expiration));
        
    }
    if(m_IdIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("id"), m_Id));
        
    }
    if(m_KindIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("kind"), m_Kind));
        
    }
    {
        std::vector<web::json::value> jsonArray;
        for( auto& item : m_Params )
        {
            web::json::value tmp = web::json::value::object();
            tmp[U("key")] = ModelBase::toJson(item.first);
            tmp[U("value")] = ModelBase::toJson(item.second);
            jsonArray.push_back(tmp);
        }
        
        if(jsonArray.size() > 0)
        {
            multipart->add(ModelBase::toHttpContent(namePrefix + U("params"), web::json::value::array(jsonArray), U("application/json")));
        }
    }
    if(m_PayloadIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("payload"), m_Payload));
    }
    if(m_ResourceIdIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("resourceId"), m_ResourceId));
        
    }
    if(m_ResourceUriIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("resourceUri"), m_ResourceUri));
        
    }
    if(m_TokenIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("token"), m_Token));
        
    }
    if(m_TypeIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("type"), m_Type));
        
    }
}

void Channel::fromMultiPart(std::shared_ptr<MultipartFormData> multipart, const utility::string_t& prefix)
{
    utility::string_t namePrefix = prefix;
    if(namePrefix.size() > 0 && namePrefix[namePrefix.size() - 1] != U('.'))
    {
        namePrefix += U(".");
    }

    if(multipart->hasContent(U("address")))
    {
        setAddress(ModelBase::stringFromHttpContent(multipart->getContent(U("address"))));
    }
    if(multipart->hasContent(U("expiration")))
    {
        setExpiration(ModelBase::stringFromHttpContent(multipart->getContent(U("expiration"))));
    }
    if(multipart->hasContent(U("id")))
    {
        setId(ModelBase::stringFromHttpContent(multipart->getContent(U("id"))));
    }
    if(multipart->hasContent(U("kind")))
    {
        setKind(ModelBase::stringFromHttpContent(multipart->getContent(U("kind"))));
    }
    {
        m_Params.clear();
        if(multipart->hasContent(U("params")))
        {

        web::json::value jsonArray = web::json::value::parse(ModelBase::stringFromHttpContent(multipart->getContent(U("params"))));
        for( auto& item : jsonArray.as_array() )
        {
            utility::string_t key;
            if(item.has_field(U("key")))
            {
                key = ModelBase::stringFromJson(item[U("key")]);
            }
            m_Params.insert(std::pair<utility::string_t,utility::string_t>( key, ModelBase::stringFromJson(item[U("value")])));
        }
        }
    }
    if(multipart->hasContent(U("payload")))
    {
        setPayload(ModelBase::boolFromHttpContent(multipart->getContent(U("payload"))));
    }
    if(multipart->hasContent(U("resourceId")))
    {
        setResourceId(ModelBase::stringFromHttpContent(multipart->getContent(U("resourceId"))));
    }
    if(multipart->hasContent(U("resourceUri")))
    {
        setResourceUri(ModelBase::stringFromHttpContent(multipart->getContent(U("resourceUri"))));
    }
    if(multipart->hasContent(U("token")))
    {
        setToken(ModelBase::stringFromHttpContent(multipart->getContent(U("token"))));
    }
    if(multipart->hasContent(U("type")))
    {
        setType(ModelBase::stringFromHttpContent(multipart->getContent(U("type"))));
    }
}

utility::string_t Channel::getAddress() const
{
    return m_Address;
}


void Channel::setAddress(utility::string_t value)
{
    m_Address = value;
    m_AddressIsSet = true;
}
bool Channel::addressIsSet() const
{
    return m_AddressIsSet;
}

void Channel::unsetAddress()
{
    m_AddressIsSet = false;
}

utility::string_t Channel::getExpiration() const
{
    return m_Expiration;
}


void Channel::setExpiration(utility::string_t value)
{
    m_Expiration = value;
    m_ExpirationIsSet = true;
}
bool Channel::expirationIsSet() const
{
    return m_ExpirationIsSet;
}

void Channel::unsetExpiration()
{
    m_ExpirationIsSet = false;
}

utility::string_t Channel::getId() const
{
    return m_Id;
}


void Channel::setId(utility::string_t value)
{
    m_Id = value;
    m_IdIsSet = true;
}
bool Channel::idIsSet() const
{
    return m_IdIsSet;
}

void Channel::unsetId()
{
    m_IdIsSet = false;
}

utility::string_t Channel::getKind() const
{
    return m_Kind;
}


void Channel::setKind(utility::string_t value)
{
    m_Kind = value;
    m_KindIsSet = true;
}
bool Channel::kindIsSet() const
{
    return m_KindIsSet;
}

void Channel::unsetKind()
{
    m_KindIsSet = false;
}

std::map<utility::string_t, utility::string_t>& Channel::getParams()
{
    return m_Params;
}

void Channel::setParams(std::map<utility::string_t, utility::string_t> value)
{
    m_Params = value;
    m_ParamsIsSet = true;
}
bool Channel::paramsIsSet() const
{
    return m_ParamsIsSet;
}

void Channel::unsetParams()
{
    m_ParamsIsSet = false;
}

bool Channel::getPayload() const
{
    return m_Payload;
}


void Channel::setPayload(bool value)
{
    m_Payload = value;
    m_PayloadIsSet = true;
}
bool Channel::payloadIsSet() const
{
    return m_PayloadIsSet;
}

void Channel::unsetPayload()
{
    m_PayloadIsSet = false;
}

utility::string_t Channel::getResourceId() const
{
    return m_ResourceId;
}


void Channel::setResourceId(utility::string_t value)
{
    m_ResourceId = value;
    m_ResourceIdIsSet = true;
}
bool Channel::resourceIdIsSet() const
{
    return m_ResourceIdIsSet;
}

void Channel::unsetResourceId()
{
    m_ResourceIdIsSet = false;
}

utility::string_t Channel::getResourceUri() const
{
    return m_ResourceUri;
}


void Channel::setResourceUri(utility::string_t value)
{
    m_ResourceUri = value;
    m_ResourceUriIsSet = true;
}
bool Channel::resourceUriIsSet() const
{
    return m_ResourceUriIsSet;
}

void Channel::unsetResourceUri()
{
    m_ResourceUriIsSet = false;
}

utility::string_t Channel::getToken() const
{
    return m_Token;
}


void Channel::setToken(utility::string_t value)
{
    m_Token = value;
    m_TokenIsSet = true;
}
bool Channel::tokenIsSet() const
{
    return m_TokenIsSet;
}

void Channel::unsetToken()
{
    m_TokenIsSet = false;
}

utility::string_t Channel::getType() const
{
    return m_Type;
}


void Channel::setType(utility::string_t value)
{
    m_Type = value;
    m_TypeIsSet = true;
}
bool Channel::typeIsSet() const
{
    return m_TypeIsSet;
}

void Channel::unsetType()
{
    m_TypeIsSet = false;
}

}
}
}
}
