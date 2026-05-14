function normalizeTicket(ticket) {
  return { id: ticket.id, label: String(ticket.label).trim() };
}

export function buildTicket(ticket) {
  return normalizeTicket(ticket);
}
