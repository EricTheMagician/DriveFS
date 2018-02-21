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



#include "Reply.h"

namespace io {
namespace swagger {
namespace client {
namespace model {

Reply::Reply()
{
    m_Action = U("");
    m_ActionIsSet = false;
    m_AuthorIsSet = false;
    m_Content = U("");
    m_ContentIsSet = false;
    m_CreatedTime = utility::datetime();
    m_CreatedTimeIsSet = false;
    m_Deleted = false;
    m_DeletedIsSet = false;
    m_HtmlContent = U("");
    m_HtmlContentIsSet = false;
    m_Id = U("");
    m_IdIsSet = false;
    m_Kind = U("");
    m_KindIsSet = false;
    m_ModifiedTime = utility::datetime();
    m_ModifiedTimeIsSet = false;
}

Reply::~Reply()
{
}

void Reply::validate()
{
    // TODO: implement validation
}

web::json::value Reply::toJson() const
{
    web::json::value val = web::json::value::object();

    if(m_ActionIsSet)
    {
        val[U("action")] = ModelBase::toJson(m_Action);
    }
    if(m_AuthorIsSet)
    {
        val[U("author")] = ModelBase::toJson(m_Author);
    }
    if(m_ContentIsSet)
    {
        val[U("content")] = ModelBase::toJson(m_Content);
    }
    if(m_CreatedTimeIsSet)
    {
        val[U("createdTime")] = ModelBase::toJson(m_CreatedTime);
    }
    if(m_DeletedIsSet)
    {
        val[U("deleted")] = ModelBase::toJson(m_Deleted);
    }
    if(m_HtmlContentIsSet)
    {
        val[U("htmlContent")] = ModelBase::toJson(m_HtmlContent);
    }
    if(m_IdIsSet)
    {
        val[U("id")] = ModelBase::toJson(m_Id);
    }
    if(m_KindIsSet)
    {
        val[U("kind")] = ModelBase::toJson(m_Kind);
    }
    if(m_ModifiedTimeIsSet)
    {
        val[U("modifiedTime")] = ModelBase::toJson(m_ModifiedTime);
    }

    return val;
}

void Reply::fromJson(web::json::value& val)
{
    if(val.has_field(U("action")))
    {
        setAction(ModelBase::stringFromJson(val[U("action")]));
    }
    if(val.has_field(U("author")))
    {
        if(!val[U("author")].is_null())
        {
            std::shared_ptr<User> newItem(new User());
            newItem->fromJson(val[U("author")]);
            setAuthor( newItem );
        }
    }
    if(val.has_field(U("content")))
    {
        setContent(ModelBase::stringFromJson(val[U("content")]));
    }
    if(val.has_field(U("createdTime")))
    {
        setCreatedTime(ModelBase::dateFromJson(val[U("createdTime")]));
    }
    if(val.has_field(U("deleted")))
    {
        setDeleted(ModelBase::boolFromJson(val[U("deleted")]));
    }
    if(val.has_field(U("htmlContent")))
    {
        setHtmlContent(ModelBase::stringFromJson(val[U("htmlContent")]));
    }
    if(val.has_field(U("id")))
    {
        setId(ModelBase::stringFromJson(val[U("id")]));
    }
    if(val.has_field(U("kind")))
    {
        setKind(ModelBase::stringFromJson(val[U("kind")]));
    }
    if(val.has_field(U("modifiedTime")))
    {
        setModifiedTime(ModelBase::dateFromJson(val[U("modifiedTime")]));
    }
}

void Reply::toMultipart(std::shared_ptr<MultipartFormData> multipart, const utility::string_t& prefix) const
{
    utility::string_t namePrefix = prefix;
    if(namePrefix.size() > 0 && namePrefix[namePrefix.size() - 1] != U('.'))
    {
        namePrefix += U(".");
    }

    if(m_ActionIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("action"), m_Action));
        
    }
    if(m_AuthorIsSet)
    {
        if (m_Author.get())
        {
            m_Author->toMultipart(multipart, U("author."));
        }
        
    }
    if(m_ContentIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("content"), m_Content));
        
    }
    if(m_CreatedTimeIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("createdTime"), m_CreatedTime));
        
    }
    if(m_DeletedIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("deleted"), m_Deleted));
    }
    if(m_HtmlContentIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("htmlContent"), m_HtmlContent));
        
    }
    if(m_IdIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("id"), m_Id));
        
    }
    if(m_KindIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("kind"), m_Kind));
        
    }
    if(m_ModifiedTimeIsSet)
    {
        multipart->add(ModelBase::toHttpContent(namePrefix + U("modifiedTime"), m_ModifiedTime));
        
    }
}

void Reply::fromMultiPart(std::shared_ptr<MultipartFormData> multipart, const utility::string_t& prefix)
{
    utility::string_t namePrefix = prefix;
    if(namePrefix.size() > 0 && namePrefix[namePrefix.size() - 1] != U('.'))
    {
        namePrefix += U(".");
    }

    if(multipart->hasContent(U("action")))
    {
        setAction(ModelBase::stringFromHttpContent(multipart->getContent(U("action"))));
    }
    if(multipart->hasContent(U("author")))
    {
        if(multipart->hasContent(U("author")))
        {
            std::shared_ptr<User> newItem(new User());
            newItem->fromMultiPart(multipart, U("author."));
            setAuthor( newItem );
        }
    }
    if(multipart->hasContent(U("content")))
    {
        setContent(ModelBase::stringFromHttpContent(multipart->getContent(U("content"))));
    }
    if(multipart->hasContent(U("createdTime")))
    {
        setCreatedTime(ModelBase::dateFromHttpContent(multipart->getContent(U("createdTime"))));
    }
    if(multipart->hasContent(U("deleted")))
    {
        setDeleted(ModelBase::boolFromHttpContent(multipart->getContent(U("deleted"))));
    }
    if(multipart->hasContent(U("htmlContent")))
    {
        setHtmlContent(ModelBase::stringFromHttpContent(multipart->getContent(U("htmlContent"))));
    }
    if(multipart->hasContent(U("id")))
    {
        setId(ModelBase::stringFromHttpContent(multipart->getContent(U("id"))));
    }
    if(multipart->hasContent(U("kind")))
    {
        setKind(ModelBase::stringFromHttpContent(multipart->getContent(U("kind"))));
    }
    if(multipart->hasContent(U("modifiedTime")))
    {
        setModifiedTime(ModelBase::dateFromHttpContent(multipart->getContent(U("modifiedTime"))));
    }
}

utility::string_t Reply::getAction() const
{
    return m_Action;
}


void Reply::setAction(utility::string_t value)
{
    m_Action = value;
    m_ActionIsSet = true;
}
bool Reply::actionIsSet() const
{
    return m_ActionIsSet;
}

void Reply::unsetAction()
{
    m_ActionIsSet = false;
}

std::shared_ptr<User> Reply::getAuthor() const
{
    return m_Author;
}


void Reply::setAuthor(std::shared_ptr<User> value)
{
    m_Author = value;
    m_AuthorIsSet = true;
}
bool Reply::authorIsSet() const
{
    return m_AuthorIsSet;
}

void Reply::unsetAuthor()
{
    m_AuthorIsSet = false;
}

utility::string_t Reply::getContent() const
{
    return m_Content;
}


void Reply::setContent(utility::string_t value)
{
    m_Content = value;
    m_ContentIsSet = true;
}
bool Reply::contentIsSet() const
{
    return m_ContentIsSet;
}

void Reply::unsetContent()
{
    m_ContentIsSet = false;
}

utility::datetime Reply::getCreatedTime() const
{
    return m_CreatedTime;
}


void Reply::setCreatedTime(utility::datetime value)
{
    m_CreatedTime = value;
    m_CreatedTimeIsSet = true;
}
bool Reply::createdTimeIsSet() const
{
    return m_CreatedTimeIsSet;
}

void Reply::unsetCreatedTime()
{
    m_CreatedTimeIsSet = false;
}

bool Reply::getDeleted() const
{
    return m_Deleted;
}


void Reply::setDeleted(bool value)
{
    m_Deleted = value;
    m_DeletedIsSet = true;
}
bool Reply::deletedIsSet() const
{
    return m_DeletedIsSet;
}

void Reply::unsetDeleted()
{
    m_DeletedIsSet = false;
}

utility::string_t Reply::getHtmlContent() const
{
    return m_HtmlContent;
}


void Reply::setHtmlContent(utility::string_t value)
{
    m_HtmlContent = value;
    m_HtmlContentIsSet = true;
}
bool Reply::htmlContentIsSet() const
{
    return m_HtmlContentIsSet;
}

void Reply::unsetHtmlContent()
{
    m_HtmlContentIsSet = false;
}

utility::string_t Reply::getId() const
{
    return m_Id;
}


void Reply::setId(utility::string_t value)
{
    m_Id = value;
    m_IdIsSet = true;
}
bool Reply::idIsSet() const
{
    return m_IdIsSet;
}

void Reply::unsetId()
{
    m_IdIsSet = false;
}

utility::string_t Reply::getKind() const
{
    return m_Kind;
}


void Reply::setKind(utility::string_t value)
{
    m_Kind = value;
    m_KindIsSet = true;
}
bool Reply::kindIsSet() const
{
    return m_KindIsSet;
}

void Reply::unsetKind()
{
    m_KindIsSet = false;
}

utility::datetime Reply::getModifiedTime() const
{
    return m_ModifiedTime;
}


void Reply::setModifiedTime(utility::datetime value)
{
    m_ModifiedTime = value;
    m_ModifiedTimeIsSet = true;
}
bool Reply::modifiedTimeIsSet() const
{
    return m_ModifiedTimeIsSet;
}

void Reply::unsetModifiedTime()
{
    m_ModifiedTimeIsSet = false;
}

}
}
}
}
