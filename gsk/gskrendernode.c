/* GSK - The GTK Scene Kit
 *
 * Copyright 2016  Endless
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:GskRenderNode
 * @title: GskRenderNode
 * @Short_desc: Simple scene graph element
 *
 * TODO
 */

#include "config.h"

#include "gskrendernodeprivate.h"

#include "gskdebugprivate.h"
#include "gskrendernodeiter.h"

#include <graphene-gobject.h>

G_DEFINE_TYPE (GskRenderNode, gsk_render_node, G_TYPE_OBJECT)

static void
gsk_render_node_dispose (GObject *gobject)
{
  GskRenderNode *self = GSK_RENDER_NODE (gobject);
  GskRenderNodeIter iter;

  gsk_render_node_iter_init (&iter, self);
  while (gsk_render_node_iter_next (&iter, NULL))
    gsk_render_node_iter_remove (&iter);

  G_OBJECT_CLASS (gsk_render_node_parent_class)->dispose (gobject);
}

static void
gsk_render_node_class_init (GskRenderNodeClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gsk_render_node_dispose;
}

static void
gsk_render_node_init (GskRenderNode *self)
{
  graphene_rect_init_from_rect (&self->bounds, graphene_rect_zero ());

  graphene_matrix_init_identity (&self->transform);
  graphene_matrix_init_identity (&self->child_transform);

  self->opacity = 1.0;

  self->is_mutable = TRUE;
}

/**
 * gsk_render_node_new:
 *
 * Creates a new #GskRenderNode, to be used with #GskRenderer.
 *
 * Returns: (transfer full): the newly created #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_new (void)
{
  return g_object_new (GSK_TYPE_RENDER_NODE, NULL);
}

/**
 * gsk_render_node_get_parent:
 * @node: a #GskRenderNode
 *
 * Returns the parent of the @node.
 *
 * Returns: (transfer none): the parent of the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_get_parent (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);

  return node->parent;
}

/**
 * gsk_render_node_get_first_child:
 * @node: a #GskRenderNode
 *
 * Returns the first child of @node.
 *
 * Returns: (transfer none): the first child of the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_get_first_child (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);

  return node->first_child;
}

/**
 * gsk_render_node_get_last_child:
 *
 * Returns the last child of @node.
 *
 * Returns: (transfer none): the last child of the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_get_last_child (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);

  return node->last_child;
}

/**
 * gsk_render_node_get_next_sibling:
 *
 * Returns the next sibling of @node.
 *
 * Returns: (transfer none): the next sibling of the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_get_next_sibling (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);

  return node->next_sibling;
}

/**
 * gsk_render_node_get_previous_sibling:
 *
 * Returns the previous sibling of @node.
 *
 * Returns: (transfer none): the previous sibling of the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_get_previous_sibling (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);

  return node->prev_sibling;
}

typedef void (* InsertChildFunc) (GskRenderNode *node,
                                  GskRenderNode *child,
                                  gpointer       user_data);

static void
gsk_render_node_insert_child_internal (GskRenderNode   *node,
                                       GskRenderNode   *child,
                                       InsertChildFunc  insert_func,
                                       gpointer         insert_func_data)
{
  if (node == child)
    {
      g_critical ("The render node of type '%s' cannot be added to itself.",
                  G_OBJECT_TYPE_NAME (node));
      return;
    }

  if (child->parent != NULL)
    {
      g_critical ("The render node of type '%s' already has a parent of type '%s'; "
                  "render nodes cannot be added to multiple parents.",
		  G_OBJECT_TYPE_NAME (child),
		  G_OBJECT_TYPE_NAME (node));
      return;
    }

  if (!node->is_mutable)
    {
      g_critical ("The render node of type '%s' is immutable.",
                  G_OBJECT_TYPE_NAME (node));
      return;
    }

  insert_func (node, child, insert_func_data);

  g_object_ref (child);

  child->parent = node;
  child->age = 0;
  child->needs_world_matrix_update = TRUE;

  node->n_children += 1;
  node->age += 1;
  node->needs_world_matrix_update = TRUE;

  if (child->prev_sibling == NULL)
    node->first_child = child;
  if (child->next_sibling == NULL)
    node->last_child = child;
}

static void
insert_child_at_pos (GskRenderNode *node,
                     GskRenderNode *child,
                     gpointer       user_data)
{
  int pos = GPOINTER_TO_INT (user_data);

  if (pos == 0)
    {
      GskRenderNode *tmp = node->first_child;

      if (tmp != NULL)
	tmp->prev_sibling = child;

      child->prev_sibling = NULL;
      child->next_sibling = tmp;

      return;
    }

  if (pos < 0 || pos >= node->n_children)
    {
      GskRenderNode *tmp = node->last_child;

      if (tmp != NULL)
	tmp->next_sibling = child;

      child->prev_sibling = tmp;
      child->next_sibling = NULL;

      return;
    }

  {
    GskRenderNode *iter;
    int i;

    for (iter = node->first_child, i = 0;
	 iter != NULL;
	 iter = iter->next_sibling, i++)
      {
	if (i == pos)
	  {
	    GskRenderNode *tmp = iter->prev_sibling;

	    child->prev_sibling = tmp;
	    child->next_sibling = iter;

	    iter->prev_sibling = child;

	    if (tmp != NULL)
	      tmp->next_sibling = child;

	    break;
	  }
      }
  }
}

/**
 * gsk_render_node_append_child:
 * @node: a #GskRenderNode
 * @child: a #GskRenderNode
 *
 * Appends @child to the list of children of @node.
 *
 * This function acquires a reference on @child.
 *
 * Returns: (transfer none): the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_append_child (GskRenderNode *node,
                              GskRenderNode *child)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (child), node);
  g_return_val_if_fail (node->is_mutable, node);

  gsk_render_node_insert_child_internal (node, child,
					 insert_child_at_pos,
					 GINT_TO_POINTER (node->n_children));

  return node;
}

/**
 * gsk_render_node_prepend_child:
 * @node: a #GskRenderNode
 * @child: a #GskRenderNode
 *
 * Prepends @child to the list of children of @node.
 *
 * This function acquires a reference on @child.
 *
 * Returns: (transfer none): the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_prepend_child (GskRenderNode *node,
                               GskRenderNode *child)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (child), node);
  g_return_val_if_fail (node->is_mutable, node);

  gsk_render_node_insert_child_internal (node, child,
                                         insert_child_at_pos,
                                         GINT_TO_POINTER (0));

  return node;
}

/**
 * gsk_render_node_insert_child_at_pos:
 * @node: a #GskRenderNode
 * @child: a #GskRenderNode
 * @index_: the index in the list of children where @child should be inserted at
 *
 * Inserts @child into the list of children of @node, using the given @index_.
 *
 * If @index_ is 0, the @child will be prepended to the list of children.
 *
 * If @index_ is less than zero, or equal to the number of children, the @child
 * will be appended to the list of children.
 *
 * This function acquires a reference on @child.
 *
 * Returns: (transfer none): the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_insert_child_at_pos (GskRenderNode *node,
                                     GskRenderNode *child,
                                     int            index_)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (child), node);
  g_return_val_if_fail (node->is_mutable, node);

  gsk_render_node_insert_child_internal (node, child,
					 insert_child_at_pos,
					 GINT_TO_POINTER (index_));

  return node;
}

static void
insert_child_before (GskRenderNode *node,
                     GskRenderNode *child,
                     gpointer       user_data)
{
  GskRenderNode *sibling = user_data;

  if (sibling == NULL)
    sibling = node->first_child;

  child->next_sibling = sibling;

  if (sibling != NULL)
    {
      GskRenderNode *tmp = sibling->prev_sibling;

      child->prev_sibling = tmp;

      if (tmp != NULL)
	tmp->next_sibling = child;

      sibling->prev_sibling = child;
    }
  else
    child->prev_sibling = NULL;
}

/**
 * gsk_render_node_insert_child_before:
 * @node: a #GskRenderNode
 * @child: a #GskRenderNode
 * @sibling: (nullable): a #GskRenderNode, or %NULL
 *
 * Inserts @child in the list of children of @node, before @sibling.
 *
 * If @sibling is %NULL, the @child will be inserted at the beginning of the
 * list of children.
 *
 * This function acquires a reference of @child.
 *
 * Returns: (transfer none): the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_insert_child_before (GskRenderNode *node,
                                     GskRenderNode *child,
                                     GskRenderNode *sibling)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (child), node);
  g_return_val_if_fail (sibling == NULL || GSK_IS_RENDER_NODE (sibling), node);
  g_return_val_if_fail (node->is_mutable, node);

  gsk_render_node_insert_child_internal (node, child, insert_child_before, sibling);

  return node;
}

static void
insert_child_after (GskRenderNode *node,
                    GskRenderNode *child,
                    gpointer       user_data)
{
  GskRenderNode *sibling = user_data;

  if (sibling == NULL)
    sibling = node->last_child;

  child->prev_sibling = sibling;

  if (sibling != NULL)
    {
      GskRenderNode *tmp = sibling->next_sibling;

      child->next_sibling = tmp;

      if (tmp != NULL)
	tmp->prev_sibling = child;

      sibling->next_sibling = child;
    }
  else
    child->next_sibling = NULL;
}

/**
 * gsk_render_node_insert_child_after:
 * @node: a #GskRenderNode
 * @child: a #GskRenderNode
 * @sibling: (nullable): a #GskRenderNode, or %NULL
 *
 * Inserts @child in the list of children of @node, after @sibling.
 *
 * If @sibling is %NULL, the @child will be inserted at the end of the list
 * of children.
 *
 * This function acquires a reference of @child.
 *
 * Returns: (transfer none): the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_insert_child_after (GskRenderNode *node,
                                    GskRenderNode *child,
                                    GskRenderNode *sibling)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (child), node);
  g_return_val_if_fail (sibling == NULL || GSK_IS_RENDER_NODE (sibling), node);
  g_return_val_if_fail (node->is_mutable, node);

  if (sibling != NULL)
    g_return_val_if_fail (sibling->parent == node, node);

  gsk_render_node_insert_child_internal (node, child, insert_child_after, sibling);

  return node;
}

typedef struct {
  GskRenderNode *prev_sibling;
  GskRenderNode *next_sibling;
} InsertBetween;

static void
insert_child_between (GskRenderNode *node,
                      GskRenderNode *child,
                      gpointer       data_)
{
  InsertBetween *data = data_;

  child->prev_sibling = data->prev_sibling;
  child->next_sibling = data->next_sibling;

  if (data->prev_sibling != NULL)
    data->prev_sibling->next_sibling = child;

  if (data->next_sibling != NULL)
    data->next_sibling->prev_sibling = child;
}

/**
 * gsk_render_node_replace_child:
 * @node: a #GskRenderNode
 * @new_child: the #GskRenderNode to add
 * @old_child: the #GskRenderNode to replace
 *
 * Replaces @old_child with @new_child in the list of children of @node.
 *
 * This function acquires a reference to @new_child, and releases a reference
 * of @old_child.
 *
 * Returns: (transfer none): the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_replace_child (GskRenderNode *node,
                               GskRenderNode *new_child,
                               GskRenderNode *old_child)
{
  InsertBetween clos;

  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (new_child), node);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (old_child), node);

  g_return_val_if_fail (new_child->parent == NULL, node);
  g_return_val_if_fail (old_child->parent == node, node);

  g_return_val_if_fail (node->is_mutable, node);

  clos.prev_sibling = old_child->prev_sibling;
  clos.next_sibling = old_child->next_sibling;
  gsk_render_node_remove_child (node, old_child);

  gsk_render_node_insert_child_internal (node, new_child, insert_child_between, &clos);

  return node;
}

/**
 * gsk_render_node_remove_child:
 * @node: a #GskRenderNode
 * @child: a #GskRenderNode child of @node
 *
 * Removes @child from the list of children of @node.
 *
 * This function releases the reference acquired when adding @child to the
 * list of children.
 *
 * Returns: (transfer none): the #GskRenderNode
 */
GskRenderNode *
gsk_render_node_remove_child (GskRenderNode *node,
                              GskRenderNode *child)
{
  GskRenderNode *prev_sibling, *next_sibling;

  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (child), node);
  g_return_val_if_fail (node->is_mutable, node);

  if (child->parent != node)
    {
      g_critical ("The render node of type '%s' is not a child of the render node of type '%s'",
		  G_OBJECT_TYPE_NAME (child),
		  G_OBJECT_TYPE_NAME (node));
      return node;
    }

  prev_sibling = child->prev_sibling;
  next_sibling = child->next_sibling;

  child->parent = NULL;
  child->prev_sibling = NULL;
  child->next_sibling = NULL;
  child->age = 0;

  if (prev_sibling)
    prev_sibling->next_sibling = next_sibling;
  if (next_sibling)
    next_sibling->prev_sibling = prev_sibling;

  node->age += 1;
  node->n_children -= 1;

  if (node->first_child == child)
    node->first_child = next_sibling;
  if (node->last_child == child)
    node->last_child = prev_sibling;

  g_object_unref (child);

  return node;
}

/**
 * gsk_render_node_remove_all_children:
 * @node: a #GskRenderNode
 *
 * Removes all children of @node.
 *
 * See also: gsk_render_node_remove_child()
 *
 * Returns: (transfer none): the #GskRenderNode
 *
 * Since: 3.22
 */
GskRenderNode *
gsk_render_node_remove_all_children (GskRenderNode *node)
{
  GskRenderNodeIter iter;

  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (node->is_mutable, node);

  if (node->n_children == 0)
    return node;

  gsk_render_node_iter_init (&iter, node);
  while (gsk_render_node_iter_next (&iter, NULL))
    gsk_render_node_iter_remove (&iter);

  g_assert (node->n_children == 0);
  g_assert (node->first_child == NULL);
  g_assert (node->last_child == NULL);

  return node;
}

/**
 * gsk_render_node_get_n_children:
 * @node: a #GskRenderNode
 *
 * Retrieves the number of direct children of @node.
 *
 * Returns: the number of children of the #GskRenderNode
 *
 * Since: 3.22
 */
guint
gsk_render_node_get_n_children (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), 0);

  return node->n_children;
}

/**
 * gsk_render_node_set_bounds:
 * @node: a #GskRenderNode
 * @bounds: (nullable): the boundaries of @node
 *
 * Sets the boundaries of @node, which describe the geometry of the
 * render node, and are used to clip the surface associated to it
 * when rendering.
 *
 * Since: 3.22
 */
void
gsk_render_node_set_bounds (GskRenderNode         *node,
                            const graphene_rect_t *bounds)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));
  g_return_if_fail (node->is_mutable);

  if (bounds == NULL)
    graphene_rect_init_from_rect (&node->bounds, graphene_rect_zero ());
  else
    graphene_rect_init_from_rect (&node->bounds, bounds);
}

/**
 * gsk_render_node_get_bounds:
 * @node: a #GskRenderNode
 * @bounds: (out caller-allocates): return location for the boundaries
 *
 * Retrieves the boundaries set using gsk_render_node_set_bounds().
 *
 * Since: 3.22
 */
void
gsk_render_node_get_bounds (GskRenderNode   *node,
                            graphene_rect_t *bounds)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));
  g_return_if_fail (bounds != NULL);

  *bounds = node->bounds;
}

/**
 * gsk_render_node_set_transform:
 * @node: a #GskRenderNode
 * @transform: (nullable): a transformation matrix
 *
 * Sets the transformation matrix used when rendering the @node.
 *
 * Since: 3.22
 */
void
gsk_render_node_set_transform (GskRenderNode           *node,
                               const graphene_matrix_t *transform)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));
  g_return_if_fail (node->is_mutable);

  if (transform == NULL)
    graphene_matrix_init_identity (&node->transform);
  else
    graphene_matrix_init_from_matrix (&node->transform, transform);

  node->transform_set = !graphene_matrix_is_identity (&node->transform);
}

/**
 * gsk_render_node_set_child_transform:
 * @node: a #GskRenderNode
 * @transform: (nullable): a transformation matrix
 *
 * Sets the transformation matrix used when rendering the children
 * of @node.
 *
 * Since: 3.22
 */
void
gsk_render_node_set_child_transform (GskRenderNode           *node,
                                     const graphene_matrix_t *transform)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));
  g_return_if_fail (node->is_mutable);

  if (transform == NULL)
    graphene_matrix_init_identity (&node->child_transform);
  else
    graphene_matrix_init_from_matrix (&node->child_transform, transform);

  node->child_transform_set = !graphene_matrix_is_identity (&node->child_transform);
}

/**
 * gsk_render_node_set_opacity:
 * @node: a #GskRenderNode
 * @opacity: the opacity of the node, between 0 (fully transparent) and
 *   1 (fully opaque)
 *
 * Sets the opacity of the @node.
 *
 * Since: 3.22
 */
void
gsk_render_node_set_opacity (GskRenderNode *node,
                             double         opacity)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));
  g_return_if_fail (node->is_mutable);

  node->opacity = CLAMP (opacity, 0.0, 1.0);
}

/**
 * gsk_render_node_get_opacity:
 * @node: a #GskRenderNode
 *
 * Retrieves the opacity set using gsk_render_node_set_opacity().
 *
 * Returns: the opacity of the #GskRenderNode
 *
 * Since: 3.22
 */
double
gsk_render_node_get_opacity (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), 0.0);

  return node->opacity;
}

/**
 * gsk_render_node_set_hidden:
 * @node: a #GskRenderNode
 * @hidden: whether the @node should be hidden or not
 *
 * Sets whether the @node should be hidden.
 *
 * Hidden nodes, and their descendants, are not rendered.
 *
 * Since: 3.22
 */
void
gsk_render_node_set_hidden (GskRenderNode *node,
                            gboolean       hidden)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));
  g_return_if_fail (node->is_mutable);

  node->hidden = !!hidden;
}

/**
 * gsk_render_node_is_hidden:
 * @node: a #GskRenderNode
 *
 * Checks whether a @node is hidden.
 *
 * Returns: %TRUE if the #GskRenderNode is hidden
 *
 * Since: 3.22
 */
gboolean
gsk_render_node_is_hidden (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), TRUE);

  return node->hidden;
}

/**
 * gsk_render_node_set_opaque:
 * @node: a #GskRenderNode
 * @opaque: whether the node is fully opaque or not
 *
 * Sets whether the node is known to be fully opaque.
 *
 * Fully opaque nodes will ignore the opacity set using gsk_render_node_set_opacity(),
 * but if their parent is not opaque they may still be rendered with an opacity.
 *
 * Renderers may use this information to optimize the rendering pipeline.
 *
 * Since: 3.22
 */
void
gsk_render_node_set_opaque (GskRenderNode *node,
                            gboolean       opaque)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));
  g_return_if_fail (node->is_mutable);

  node->opaque = !!opaque;
}

/**
 * gsk_render_node_is_opaque:
 * @node: a #GskRenderNode
 *
 * Retrieves the value set using gsk_render_node_set_opaque().
 *
 * Returns: %TRUE if the #GskRenderNode is fully opaque
 *
 * Since: 3.22
 */
gboolean
gsk_render_node_is_opaque (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), TRUE);

  return node->opaque;
}

/**
 * gsk_render_node_contains:
 * @node: a #GskRenderNode
 * @descendant: a #GskRenderNode
 *
 * Checks whether @node contains @descendant.
 *
 * Returns: %TRUE if the #GskRenderNode contains the given
 *   descendant
 *
 * Since: 3.22
 */
gboolean
gsk_render_node_contains (GskRenderNode *node,
			  GskRenderNode *descendant)
{
  GskRenderNode *tmp;

  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), FALSE);
  g_return_val_if_fail (GSK_IS_RENDER_NODE (descendant), FALSE);

  for (tmp = descendant; tmp != NULL; tmp = tmp->parent)
    if (tmp == node)
      return TRUE;

  return FALSE;
}

/*< private >
 * gsk_render_node_get_toplevel:
 * @node: a #GskRenderNode
 *
 * Retrieves the top level #GskRenderNode without a parent.
 *
 * Returns: (transfer none): the top level #GskRenderNode
 */
GskRenderNode *
gsk_render_node_get_toplevel (GskRenderNode *node)
{
  GskRenderNode *parent;

  parent = node->parent;
  if (parent == NULL)
    return node;

  while (parent != NULL)
    {
      if (parent->parent == NULL)
        return parent;

      parent = parent->parent;
    }

  return NULL;
}

/*< private >
 * gsk_render_node_update_world_matrix:
 * @node: a #GskRenderNode
 * @force: %TRUE if the update should be forced
 *
 * Updates the cached world matrix of @node and its children, if needed.
 */
void
gsk_render_node_update_world_matrix (GskRenderNode *node,
                                     gboolean       force)
{
  GskRenderNodeIter iter;
  GskRenderNode *child;

  if (force || node->needs_world_matrix_update)
    {
      GSK_NOTE (RENDER_NODE, g_print ("Updating cached world matrix on node %p [parent=%p, t_set=%s, ct_set=%s]\n",
                                      node,
                                      node->parent != NULL ? node->parent : 0,
                                      node->transform_set ? "y" : "n",
                                      node->parent != NULL && node->parent->child_transform_set ? "y" : "n"));

      if (node->parent == NULL)
        {
          if (node->transform_set)
            graphene_matrix_init_from_matrix (&node->world_matrix, &node->transform);
          else
            graphene_matrix_init_identity (&node->world_matrix);
        }
      else
        {
          GskRenderNode *parent = node->parent;
          graphene_matrix_t tmp;

          if (parent->child_transform_set)
            graphene_matrix_init_from_matrix (&tmp, &parent->child_transform);
          else
            graphene_matrix_init_identity (&tmp);

          if (node->transform_set)
            graphene_matrix_multiply (&tmp, &node->transform, &tmp);

          graphene_matrix_multiply (&tmp, &parent->world_matrix, &node->world_matrix);
        }

      node->needs_world_matrix_update = FALSE;
    }

  gsk_render_node_iter_init (&iter, node);
  while (gsk_render_node_iter_next (&iter, &child))
    gsk_render_node_update_world_matrix (child, TRUE);
}

/*< private >
 * gsk_render_node_get_surface:
 * @node: a #GskRenderNode
 *
 * Retrieves the surface set using gsk_render_node_set_surface().
 *
 * Returns: (transfer none) (nullable): a Cairo surface
 */
cairo_surface_t *
gsk_render_node_get_surface (GskRenderNode *node)
{
  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);

  return node->surface;
}

/*< private >
 * gsk_render_node_get_world_matrix:
 * @node: a #GskRenderNode
 * @mv: (out caller-allocates): return location for the modelview matrix
 *   in world-relative coordinates
 *
 * Retrieves the modelview matrix in world-relative coordinates.
 */
void
gsk_render_node_get_world_matrix (GskRenderNode     *node,
                                  graphene_matrix_t *mv)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));
  g_return_if_fail (mv != NULL);

  if (node->needs_world_matrix_update)
    {
      GskRenderNode *tmp = gsk_render_node_get_toplevel (node);

      gsk_render_node_update_world_matrix (tmp, TRUE);

      g_assert (!node->needs_world_matrix_update);
    }

  *mv = node->world_matrix;
}

/**
 * gsk_render_node_set_name:
 * @node: a #GskRenderNode
 * @name: (nullable): a name for the node
 *
 * Sets the name of the node.
 *
 * A name is generally useful for debugging purposes.
 *
 * Since: 3.22
 */
void
gsk_render_node_set_name (GskRenderNode *node,
                          const char    *name)
{
  g_return_if_fail (GSK_IS_RENDER_NODE (node));

  g_free (node->name);
  node->name = g_strdup (name);
}

/**
 * gsk_render_node_get_draw_context:
 * @node: a #GskRenderNode
 *
 * Creates a Cairo context for drawing using the surface associated
 * to the render node.
 *
 * Returns: (transfer full): a Cairo context used for drawing; use
 *   cairo_destroy() when done drawing
 *
 * Since: 3.22
 */
cairo_t *
gsk_render_node_get_draw_context (GskRenderNode *node)
{
  cairo_t *res;

  g_return_val_if_fail (GSK_IS_RENDER_NODE (node), NULL);
  g_return_val_if_fail (node->is_mutable, NULL);

  if (node->surface == NULL)
    node->surface = cairo_image_surface_create (node->opaque ? CAIRO_FORMAT_RGB24
                                                             : CAIRO_FORMAT_ARGB32,
                                                node->bounds.size.width,
                                                node->bounds.size.height);

  res = cairo_create (node->surface);

  cairo_rectangle (res,
                   node->bounds.origin.x, node->bounds.origin.y,
                   node->bounds.size.width, node->bounds.size.height);
  cairo_clip (res);

  return res;
}

void
gsk_render_node_make_immutable (GskRenderNode *node)
{
  GskRenderNodeIter iter;
  GskRenderNode *child;

  if (!node->is_mutable)
    return;

  node->is_mutable = FALSE;

  gsk_render_node_iter_init (&iter, node);
  while (gsk_render_node_iter_next (&iter, &child))
    gsk_render_node_make_immutable (child);
}