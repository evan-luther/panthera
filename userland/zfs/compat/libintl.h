#ifndef PANTHERA_ZFS_COMPAT_LIBINTL_H
#define PANTHERA_ZFS_COMPAT_LIBINTL_H

static inline const char *
gettext(const char *message)
{
  return message;
}

static inline const char *
dgettext(const char *domain, const char *message)
{
  (void)domain;
  return message;
}

static inline const char *
dcgettext(const char *domain, const char *message, int category)
{
  (void)domain;
  (void)category;
  return message;
}

static inline char *
textdomain(const char *domain)
{
  return (char *)domain;
}

static inline char *
bindtextdomain(const char *domain, const char *directory)
{
  (void)directory;
  return (char *)domain;
}

#endif
